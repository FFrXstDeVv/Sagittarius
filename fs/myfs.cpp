// Sagittarius SGS-1 -- MyFS v0.2
// CoW + zstd сжатие (lossless, level 1) + FUSE write support

#include "myfs.h"
#include <zstd.h>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>

// ════════════════════════════════════════════════════════
//  Открытие / форматирование
// ════════════════════════════════════════════════════════
bool MyFS::open(const std::string& path, bool format) {
    auto mode = std::ios::in | std::ios::out | std::ios::binary;
    if (format) mode |= std::ios::trunc;
    disk_.open(path, mode);
    if (!disk_.is_open()) return false;
    return format ? formatDisk() : mountDisk();
}

void MyFS::close() {
    writeSuperBlock();
    if (disk_.is_open()) disk_.close();
}

bool MyFS::formatDisk() {
    std::vector<uint8_t> zero(MYFS_BLOCK_SIZE, 0);
    for (uint32_t i = 0; i < MYFS_TOTAL_BLOCKS; i++) {
        disk_.seekp(i * MYFS_BLOCK_SIZE);
        disk_.write((char*)zero.data(), MYFS_BLOCK_SIZE);
    }
    memset(&sb_, 0, sizeof(sb_));
    sb_.magic             = MYFS_MAGIC;
    sb_.block_size        = MYFS_BLOCK_SIZE;
    sb_.total_blocks      = MYFS_TOTAL_BLOCKS;
    sb_.free_blocks       = MYFS_TOTAL_BLOCKS - DATA_START;
    sb_.inode_table_start = INODE_TABLE_BLOCK;
    sb_.data_start        = DATA_START;
    sb_.generation        = 1;
    sb_.tx_state          = TxState::CLEAN;
    sb_.tx_old_block      = MYFS_NULL_BLOCK;
    sb_.tx_new_block      = MYFS_NULL_BLOCK;
    strncpy(sb_.label, "Sagittarius-SGS1", 32);
    writeSuperBlock();

    memset(&bitmap_, 0, sizeof(bitmap_));
    for (uint32_t i = 0; i < DATA_START; i++) setBit(i, true);
    writeBitmap();

    Inode root{};
    root.used = 1; root.type = 1; root.generation = 1;
    strncpy(root.name, "/", MYFS_MAX_NAME);
    for (auto& d : root.direct)     d = MYFS_NULL_BLOCK;
    for (auto& d : root.cow_shadow) d = MYFS_NULL_BLOCK;
    writeInode(0, root);
    sb_.inode_count = 1;
    writeSuperBlock();

    log("OK", "Отформатировано: " + std::to_string(MYFS_TOTAL_BLOCKS * MYFS_BLOCK_SIZE / 1024) + " KB");
    return true;
}

bool MyFS::mountDisk() {
    disk_.seekg(0);
    disk_.read((char*)&sb_, MYFS_BLOCK_SIZE);
    if (sb_.magic != MYFS_MAGIC) { log("ERR", "Неверная сигнатура"); return false; }
    disk_.seekg(MYFS_BLOCK_SIZE);
    disk_.read((char*)&bitmap_, MYFS_BLOCK_SIZE);
    recoverIfNeeded();
    sb_.generation++;
    writeSuperBlock();
    log("OK", "Смонтировано [" + std::string(sb_.label) + "] gen=" +
              std::to_string(sb_.generation) + " свободно=" +
              std::to_string(sb_.free_blocks * MYFS_BLOCK_SIZE / 1024) + "KB");
    return true;
}

// ════════════════════════════════════════════════════════
//  Сжатие (zstd level 1 — быстро, lossless, мало нагрузки)
// ════════════════════════════════════════════════════════
std::vector<uint8_t> MyFS::compressData(const std::string& data, bool& did_compress) {
    size_t bound = ZSTD_compressBound(data.size());
    std::vector<uint8_t> buf(bound);
    size_t csize = ZSTD_compress(buf.data(), bound, data.data(), data.size(), ZSTD_LEVEL);
    if (ZSTD_isError(csize) || csize >= data.size()) {
        // Сжатие не дало выигрыша — храним как есть
        did_compress = false;
        return std::vector<uint8_t>(data.begin(), data.end());
    }
    did_compress = true;
    buf.resize(csize);
    return buf;
}

std::string MyFS::decompressData(const uint8_t* data, size_t csize, size_t orig_size) {
    std::string out(orig_size, '\0');
    size_t result = ZSTD_decompress(out.data(), orig_size, data, csize);
    if (ZSTD_isError(result)) { log("ERR", "Ошибка декомпрессии"); return ""; }
    return out;
}

// ════════════════════════════════════════════════════════
//  Внутренняя запись файла (CoW + сжатие)
// ════════════════════════════════════════════════════════
bool MyFS::writeFileInternal(int32_t idx, Inode& node, const std::string& content) {
    // Пробуем сжать
    bool did_compress = false;
    std::vector<uint8_t> compressed;

    if (content.size() > 4096) {
        compressed = compressData(content, did_compress);
    } else {
        compressed = std::vector<uint8_t>(content.begin(), content.end());
    }

    const uint8_t* writeData = compressed.data();
    size_t writeSize = compressed.size();
    uint32_t needed = (writeSize + MYFS_BLOCK_SIZE - 1) / MYFS_BLOCK_SIZE;
    if (needed == 0) needed = 1;

    // CoW: ищем новые блоки
    int32_t newStart = findExtent(needed);
    if (newStart == -1) { log("ERR", "Нет места"); return false; }

    // PENDING в суперблок
    sb_.tx_state     = TxState::PENDING;
    sb_.tx_old_block = node.direct[0];
    sb_.tx_new_block = newStart;
    sb_.tx_inode_idx = idx;
    writeSuperBlock();

    // Пишем в новые блоки
    for (uint32_t i = 0; i < needed && i < MYFS_MAX_DIRECT; i++) {
        node.cow_shadow[i] = newStart + i;
        setBit(newStart + i, true);
        size_t off   = i * MYFS_BLOCK_SIZE;
        size_t chunk = std::min((size_t)MYFS_BLOCK_SIZE,
                                writeSize > off ? writeSize - off : 0);
        writeRawBlock(newStart + i, writeData + off, chunk);
    }
    node.cow_pending = 1;
    writeInode(idx, node);

    // COMMIT: переключаем указатели
    std::vector<uint32_t> oldBlocks(node.direct, node.direct + node.blocks_used);
    for (uint32_t i = 0; i < needed && i < MYFS_MAX_DIRECT; i++)
        node.direct[i] = node.cow_shadow[i];
    for (uint32_t i = needed; i < MYFS_MAX_DIRECT; i++)
        node.direct[i] = MYFS_NULL_BLOCK;

    node.size        = content.size();
    node.size_on_disk= writeSize;
    node.blocks_used = needed;
    node.cow_pending = 0;
    node.compression = did_compress ? COMPRESS_ZSTD : COMPRESS_NONE;
    node.generation++;
    for (auto& d : node.cow_shadow) d = MYFS_NULL_BLOCK;
    writeInode(idx, node);

    // Освобождаем старые блоки
    for (uint32_t b : oldBlocks) {
        if (b != MYFS_NULL_BLOCK) { setBit(b, false); sb_.free_blocks++; }
    }

    // Статистика сжатия
    if (did_compress) {
        sb_.compressed_blocks++;
        sb_.saved_bytes += (content.size() - writeSize);
    }

    sb_.tx_state     = TxState::CLEAN;
    sb_.tx_old_block = MYFS_NULL_BLOCK;
    sb_.tx_new_block = MYFS_NULL_BLOCK;
    writeSuperBlock();
    writeBitmap();
    return true;
}

// ════════════════════════════════════════════════════════
//  Публичные операции
// ════════════════════════════════════════════════════════
bool MyFS::createFile(const std::string& path, const std::string& content) {
    auto [parent, name] = splitPath(path);
    if (!dirExists(parent)) { log("ERR", "Папка не найдена: " + parent); return false; }
    if (inodeByPath(path) != -1) { log("ERR", "Уже существует"); return false; }

    int32_t idx = allocInode();
    if (idx == -1) { log("ERR", "Нет inode"); return false; }

    Inode node{};
    node.used = 1; node.type = 0; node.generation = 1;
    strncpy(node.name, name.c_str(), MYFS_MAX_NAME);
    strncpy(node.parent_path, parent.c_str(), 112);
    for (auto& d : node.direct)     d = MYFS_NULL_BLOCK;
    for (auto& d : node.cow_shadow) d = MYFS_NULL_BLOCK;
    writeInode(idx, node);
    sb_.inode_count++;

    if (!content.empty()) {
        if (!writeFileInternal(idx, node, content)) return false;
    } else {
        writeSuperBlock();
    }

    bool compressed = node.compression == COMPRESS_ZSTD;
    log("OK", "Файл: " + path + " " + std::to_string(node.size) + "B" +
              (compressed ? " [zstd " + std::to_string(node.size_on_disk) + "B на диске]" : ""));
    return true;
}

bool MyFS::writeFile(const std::string& path, const std::string& content) {
    int32_t idx = inodeByPath(path);
    if (idx == -1) return createFile(path, content);
    Inode node = readInode(idx);
    if (node.type != 0) { log("ERR", "Это директория"); return false; }
    bool ok = writeFileInternal(idx, node, content);
    if (ok) log("OK", "Записано: " + path + " gen=" + std::to_string(node.generation));
    return ok;
}

std::string MyFS::readFile(const std::string& path) {
    int32_t idx = inodeByPath(path);
    if (idx == -1) { log("ERR", "Не найден: " + path); return ""; }
    return readFileByInode(readInode(idx));
}

std::string MyFS::readFileByInode(const Inode& node) {
    if (node.type != 0) return "";
    // Читаем сырые данные с диска
    std::vector<uint8_t> raw;
    raw.reserve(node.size_on_disk ? node.size_on_disk : node.size);
    for (uint32_t i = 0; i < node.blocks_used && node.direct[i] != MYFS_NULL_BLOCK; i++) {
        uint8_t buf[MYFS_BLOCK_SIZE] = {};
        disk_.seekg(node.direct[i] * MYFS_BLOCK_SIZE);
        disk_.read((char*)buf, MYFS_BLOCK_SIZE);
        size_t already = raw.size();
        size_t diskSize = node.size_on_disk ? node.size_on_disk : node.size;
        size_t chunk = std::min((size_t)MYFS_BLOCK_SIZE, diskSize > already ? diskSize - already : 0);
        raw.insert(raw.end(), buf, buf + chunk);
    }
    if (node.compression == COMPRESS_ZSTD)
        return decompressData(raw.data(), raw.size(), node.size);
    return std::string(raw.begin(), raw.end());
}

bool MyFS::createDir(const std::string& path) {
    auto [parent, name] = splitPath(path);
    if (path != "/" && !dirExists(parent)) { log("ERR", "Родитель не найден: " + parent); return false; }
    if (inodeByPath(path) != -1) { log("ERR", "Уже существует"); return false; }
    int32_t idx = allocInode();
    if (idx == -1) { log("ERR", "Нет inode"); return false; }
    Inode node{};
    node.used = 1; node.type = 1; node.generation = 1;
    strncpy(node.name, name.c_str(), MYFS_MAX_NAME);
    strncpy(node.parent_path, parent.c_str(), 112);
    for (auto& d : node.direct)     d = MYFS_NULL_BLOCK;
    for (auto& d : node.cow_shadow) d = MYFS_NULL_BLOCK;
    writeInode(idx, node);
    sb_.inode_count++;
    writeSuperBlock();
    log("OK", "Папка: " + path);
    return true;
}

bool MyFS::remove(const std::string& path) {
    int32_t idx = inodeByPath(path);
    if (idx == -1) { log("ERR", "Не найден: " + path); return false; }
    Inode node = readInode(idx);
    for (uint32_t i = 0; i < node.blocks_used && node.direct[i] != MYFS_NULL_BLOCK; i++) {
        setBit(node.direct[i], false); sb_.free_blocks++;
    }
    node.used = 0;
    writeInode(idx, node);
    sb_.inode_count--;
    writeSuperBlock(); writeBitmap();
    log("OK", "Удалено: " + path);
    return true;
}

// ════════════════════════════════════════════════════════
//  FUSE write операции
// ════════════════════════════════════════════════════════
bool MyFS::mkdirFuse(const std::string& path)  { return createDir(path); }
bool MyFS::createFuse(const std::string& path) { return createFile(path, ""); }
bool MyFS::unlinkFuse(const std::string& path) { return remove(path); }

bool MyFS::truncFuse(const std::string& path, off_t size) {
    int32_t idx = inodeByPath(path);
    if (idx == -1) return false;
    Inode node = readInode(idx);
    if (size == 0) {
        // Освобождаем все блоки
        for (uint32_t i = 0; i < node.blocks_used && node.direct[i] != MYFS_NULL_BLOCK; i++) {
            setBit(node.direct[i], false); sb_.free_blocks++;
            node.direct[i] = MYFS_NULL_BLOCK;
        }
        node.size = 0; node.size_on_disk = 0; node.blocks_used = 0;
        node.compression = COMPRESS_NONE;
        writeInode(idx, node);
        writeSuperBlock(); writeBitmap();
    }
    return true;
}

int MyFS::writeFuse(const std::string& path, const char* buf, size_t size, off_t offset) {
    int32_t idx = inodeByPath(path);
    if (idx == -1) return -1;
    Inode node = readInode(idx);

    // Не читаем если файл пустой — избегаем декомпрессии нуля
    std::string current;
    if (node.size > 0 && node.blocks_used > 0) {
        node.compression = COMPRESS_NONE; // читаем как есть
        current = readFileByInode(node);
    }

    if (offset + (off_t)size > (off_t)current.size())
        current.resize(offset + size, '\0');
    memcpy(current.data() + offset, buf, size);

    node.compression = COMPRESS_NONE;
    writeFileInternal(idx, node, current);
    return (int)size;
}

// ════════════════════════════════════════════════════════
//  Crash recovery
// ════════════════════════════════════════════════════════
void MyFS::recoverIfNeeded() {
    if (sb_.tx_state == TxState::CLEAN) return;
    log("WARN", "Незавершённая транзакция! Восстановление...");
    if (sb_.tx_state == TxState::PENDING) {
        Inode node = readInode(sb_.tx_inode_idx);
        for (auto& d : node.cow_shadow) {
            if (d != MYFS_NULL_BLOCK) { setBit(d, false); sb_.free_blocks++; d = MYFS_NULL_BLOCK; }
        }
        node.cow_pending = 0;
        writeInode(sb_.tx_inode_idx, node);
        log("OK", "Откат выполнен");
    } else if (sb_.tx_state == TxState::COMMITTED) {
        if (sb_.tx_old_block != MYFS_NULL_BLOCK) { setBit(sb_.tx_old_block, false); sb_.free_blocks++; }
        log("OK", "Очистка старых блоков");
    }
    sb_.tx_state = TxState::CLEAN;
    sb_.tx_old_block = sb_.tx_new_block = MYFS_NULL_BLOCK;
    writeSuperBlock(); writeBitmap();
}

// ════════════════════════════════════════════════════════
//  Compaction
// ════════════════════════════════════════════════════════
void MyFS::compact() {
    log("..", "Compaction...");
    uint32_t writeHead = DATA_START, moved = 0;
    for (uint32_t b = DATA_START; b < MYFS_TOTAL_BLOCKS; b++) {
        if (!getBit(b)) continue;
        if (b == writeHead) { writeHead++; continue; }
        char buf[MYFS_BLOCK_SIZE];
        disk_.seekg(b * MYFS_BLOCK_SIZE); disk_.read(buf, MYFS_BLOCK_SIZE);
        disk_.seekp(writeHead * MYFS_BLOCK_SIZE); disk_.write(buf, MYFS_BLOCK_SIZE);
        uint32_t total = INODE_TABLE_BLOCKS * INODES_PER_BLOCK;
        for (uint32_t i = 0; i < total; i++) {
            Inode n = readInode(i);
            if (!n.used) continue;
            bool changed = false;
            for (auto& d : n.direct)     { if (d == b) { d = writeHead; changed = true; } }
            for (auto& d : n.cow_shadow) { if (d == b) { d = writeHead; changed = true; } }
            if (changed) writeInode(i, n);
        }
        setBit(writeHead, true); setBit(b, false);
        writeHead++; moved++;
    }
    writeBitmap();
    log("OK", "Compaction: перемещено " + std::to_string(moved) + " блоков");
}

// ════════════════════════════════════════════════════════
//  Вывод
// ════════════════════════════════════════════════════════
void MyFS::ls(const std::string& path) {
    std::cout << "\n" << path << "\n" << std::string(48, '-') << "\n";
    uint32_t total = INODE_TABLE_BLOCKS * INODES_PER_BLOCK;
    int found = 0;
    for (uint32_t i = 0; i < total; i++) {
        Inode n = readInode(i);
        if (!n.used || std::string(n.parent_path) != path) continue;
        std::cout << (n.type ? "DIR  " : "FILE ")
                  << std::left << std::setw(28) << n.name
                  << std::right << std::setw(8) << n.size << "B";
        if (n.compression == COMPRESS_ZSTD)
            std::cout << " [z:" << n.size_on_disk << "B -"
                      << (n.size > 0 ? (n.size - n.size_on_disk) * 100 / n.size : 0) << "%]";
        std::cout << " gen=" << n.generation << "\n";
        found++;
    }
    if (!found) std::cout << "  (пусто)\n";
}

void MyFS::printMap() {
    std::cout << "\nКарта диска:\n[";
    for (uint32_t i = 0; i < MYFS_TOTAL_BLOCKS; i += 8) {
        if      (i < DATA_START) std::cout << "S";
        else if (getBit(i))      std::cout << "#";
        else                     std::cout << ".";
    }
    std::cout << "]\n";
    uint32_t used = 0;
    for (uint32_t i = DATA_START; i < MYFS_TOTAL_BLOCKS; i++) if (getBit(i)) used++;
    std::cout << "Занято=" << used*MYFS_BLOCK_SIZE/1024 << "KB"
              << " Своб=" << (MYFS_TOTAL_BLOCKS-DATA_START-used)*MYFS_BLOCK_SIZE/1024 << "KB"
              << " Фраг=" << countFragments()
              << " CoW=" << (sb_.tx_state == TxState::CLEAN ? "CLEAN" : "PENDING") << "\n\n";
}

void MyFS::info() {
    std::cout << "\n=== Sagittarius MyFS v0.2 ===\n"
              << "Метка:       " << sb_.label << "\n"
              << "Generation:  " << sb_.generation << "\n"
              << "Блоков:      " << sb_.total_blocks << " x " << sb_.block_size << "B\n"
              << "Inode:       " << sb_.inode_count << "\n"
              << "Свободно:    " << sb_.free_blocks * MYFS_BLOCK_SIZE / 1024 << " KB\n"
              << "Сжато блоков:" << sb_.compressed_blocks << "\n"
              << "Сэкономлено: " << sb_.saved_bytes / 1024 << " KB\n"
              << "CoW:         " << (sb_.tx_state == TxState::CLEAN ? "CLEAN" : "PENDING") << "\n\n";
}

// ════════════════════════════════════════════════════════
//  FUSE stat / listDir
// ════════════════════════════════════════════════════════
MyFS::StatInfo MyFS::stat(const std::string& path) {
    if (path == "/") return {true, true, 0, 0};
    int32_t idx = inodeByPath(path);
    if (idx == -1) return {false, false, 0, 0};
    Inode n = readInode(idx);
    return {true, n.type == 1, n.size, n.generation};
}

std::vector<Inode> MyFS::listDir(const std::string& path) {
    std::vector<Inode> result;
    uint32_t total = INODE_TABLE_BLOCKS * INODES_PER_BLOCK;
    for (uint32_t i = 0; i < total; i++) {
        Inode n = readInode(i);
        if (n.used && std::string(n.parent_path) == path) result.push_back(n);
    }
    return result;
}

// ════════════════════════════════════════════════════════
//  Служебные
// ════════════════════════════════════════════════════════
void MyFS::setBit(uint32_t b, bool v) {
    if (v) bitmap_.bits[b/8] |=  (1<<(b%8));
    else   bitmap_.bits[b/8] &= ~(1<<(b%8));
}
bool MyFS::getBit(uint32_t b) const { return (bitmap_.bits[b/8]>>(b%8))&1; }

int32_t MyFS::findExtent(uint32_t needed) {
    uint32_t start = MYFS_NULL_BLOCK, count = 0;
    for (uint32_t i = DATA_START; i < MYFS_TOTAL_BLOCKS; i++) {
        if (!getBit(i)) { if (start==MYFS_NULL_BLOCK) start=i; if (++count>=needed) return start; }
        else            { start=MYFS_NULL_BLOCK; count=0; }
    }
    return -1;
}

int32_t MyFS::allocInode() {
    uint32_t total = INODE_TABLE_BLOCKS * INODES_PER_BLOCK;
    for (uint32_t i = 0; i < total; i++) { Inode n = readInode(i); if (!n.used) return i; }
    return -1;
}

Inode MyFS::readInode(uint32_t idx) {
    uint32_t block  = INODE_TABLE_BLOCK + idx / INODES_PER_BLOCK;
    uint32_t offset = (idx % INODES_PER_BLOCK) * sizeof(Inode);
    Inode node{};
    disk_.seekg(block * MYFS_BLOCK_SIZE + offset);
    disk_.read((char*)&node, sizeof(Inode));
    return node;
}

void MyFS::writeInode(uint32_t idx, const Inode& node) {
    uint32_t block  = INODE_TABLE_BLOCK + idx / INODES_PER_BLOCK;
    uint32_t offset = (idx % INODES_PER_BLOCK) * sizeof(Inode);
    disk_.seekp(block * MYFS_BLOCK_SIZE + offset);
    disk_.write((const char*)&node, sizeof(Inode));
}

void MyFS::writeRawBlock(uint32_t block, const void* data, size_t len) {
    std::vector<char> buf(MYFS_BLOCK_SIZE, 0);
    if (data && len) memcpy(buf.data(), data, std::min(len, (size_t)MYFS_BLOCK_SIZE));
    disk_.seekp(block * MYFS_BLOCK_SIZE);
    disk_.write(buf.data(), MYFS_BLOCK_SIZE);
}

void MyFS::writeSuperBlock() { disk_.seekp(0); disk_.write((char*)&sb_, MYFS_BLOCK_SIZE); disk_.flush(); }
void MyFS::writeBitmap()     { disk_.seekp(MYFS_BLOCK_SIZE); disk_.write((char*)&bitmap_, MYFS_BLOCK_SIZE); disk_.flush(); }

int32_t MyFS::inodeByPath(const std::string& path) {
    if (path == "/") return 0;
    auto [parent, name] = splitPath(path);
    uint32_t total = INODE_TABLE_BLOCKS * INODES_PER_BLOCK;
    for (uint32_t i = 0; i < total; i++) {
        Inode n = readInode(i);
        if (n.used && std::string(n.name)==name && std::string(n.parent_path)==parent) return i;
    }
    return -1;
}

bool MyFS::dirExists(const std::string& path) {
    if (path == "/" || path.empty()) return true;
    int32_t idx = inodeByPath(path);
    return idx != -1 && readInode(idx).type == 1;
}

std::pair<std::string,std::string> MyFS::splitPath(const std::string& path) {
    size_t pos = path.rfind('/');
    if (pos == std::string::npos || pos == 0)
        return {"/", path.substr(pos==std::string::npos?0:1)};
    return {path.substr(0, pos), path.substr(pos+1)};
}

uint32_t MyFS::countFragments() {
    uint32_t frags = 0; bool inFree = false;
    for (uint32_t i = DATA_START; i < MYFS_TOTAL_BLOCKS; i++) {
        if (!getBit(i) && !inFree) { frags++; inFree=true; }
        else if (getBit(i))         inFree=false;
    }
    return frags;
}

void MyFS::log(const std::string& level, const std::string& msg) {
    std::cout << "[" << std::setw(4) << level << "] " << msg << "\n";
}