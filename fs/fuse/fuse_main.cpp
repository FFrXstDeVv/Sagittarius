// Sagittarius SGS-1 -- MyFS FUSE (read + write)
// Сборка: clang++ -std=c++20 -O2 -o myfs_fuse src/fuse_main.cpp src/myfs.cpp
//         -Iinclude $(pkg-config fuse3 --cflags --libs) -lzstd

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <cstring>
#include <cerrno>
#include <iostream>
#include "myfs.h"

static MyFS* g_fs = nullptr;

// ── getattr ─────────────────────────────────────────────
static int sgs_getattr(const char* path, struct stat* st, struct fuse_file_info*) {
    memset(st, 0, sizeof(*st));
    auto info = g_fs->stat(path);
    if (!info.exists) return -ENOENT;
    if (info.is_dir) {
        st->st_mode  = S_IFDIR | 0755;
        st->st_nlink = 2;
    } else {
        st->st_mode  = S_IFREG | 0644; // теперь rw
        st->st_nlink = 1;
        st->st_size  = info.size;
    }
    return 0;
}

// ── readdir ──────────────────────────────────────────────
static int sgs_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                       off_t, struct fuse_file_info*, enum fuse_readdir_flags) {
    filler(buf, ".",  nullptr, 0, FUSE_FILL_DIR_PLUS);
    filler(buf, "..", nullptr, 0, FUSE_FILL_DIR_PLUS);
    for (const auto& n : g_fs->listDir(path)) {
        struct stat st{};
        st.st_mode = (n.type==1) ? (S_IFDIR|0755) : (S_IFREG|0644);
        st.st_size = n.size;
        filler(buf, n.name, &st, 0, FUSE_FILL_DIR_PLUS);
    }
    return 0;
}

// ── open ─────────────────────────────────────────────────
static int sgs_open(const char* path, struct fuse_file_info*) {
    auto info = g_fs->stat(path);
    if (!info.exists) return -ENOENT;
    if (info.is_dir)  return -EISDIR;
    return 0;
}

// ── read ─────────────────────────────────────────────────
static int sgs_read(const char* path, char* buf, size_t size, off_t offset,
                    struct fuse_file_info*) {
    std::string content = g_fs->readFile(path);
    if (content.empty() && g_fs->stat(path).size > 0) return -EIO;
    if (offset >= (off_t)content.size()) return 0;
    size_t avail  = content.size() - offset;
    size_t tocopy = std::min(size, avail);
    memcpy(buf, content.data() + offset, tocopy);
    return (int)tocopy;
}

// ── write ────────────────────────────────────────────────
static int sgs_write(const char* path, const char* buf, size_t size, off_t offset,
                     struct fuse_file_info*) {
    return g_fs->writeFuse(path, buf, size, offset);
}

// ── create ───────────────────────────────────────────────
static int sgs_create(const char* path, mode_t, struct fuse_file_info*) {
    return g_fs->createFuse(path) ? 0 : -EIO;
}

// ── mkdir ────────────────────────────────────────────────
static int sgs_mkdir(const char* path, mode_t) {
    return g_fs->mkdirFuse(path) ? 0 : -EIO;
}

// ── unlink ───────────────────────────────────────────────
static int sgs_unlink(const char* path) {
    return g_fs->unlinkFuse(path) ? 0 : -ENOENT;
}

// ── rmdir ────────────────────────────────────────────────
static int sgs_rmdir(const char* path) {
    return g_fs->unlinkFuse(path) ? 0 : -ENOENT;
}

// ── truncate ─────────────────────────────────────────────
static int sgs_truncate(const char* path, off_t size, struct fuse_file_info*) {
    return g_fs->truncFuse(path, size) ? 0 : -ENOENT;
}

// ── statfs ───────────────────────────────────────────────
static int sgs_statfs(const char*, struct statvfs* st) {
    memset(st, 0, sizeof(*st));
    st->f_bsize   = MYFS_BLOCK_SIZE;
    st->f_blocks  = MYFS_TOTAL_BLOCKS;
    st->f_namemax = MYFS_MAX_NAME - 1;
    return 0;
}

static const struct fuse_operations sgs_ops = {
    .getattr  = sgs_getattr,
    .mkdir    = sgs_mkdir,
    .unlink   = sgs_unlink,
    .rmdir    = sgs_rmdir,
    .truncate = sgs_truncate,
    .open     = sgs_open,
    .read     = sgs_read,
    .write    = sgs_write,
    .statfs   = sgs_statfs,
    .readdir  = sgs_readdir,
    .create   = sgs_create,
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Использование: myfs_fuse <образ.img> <точка монтирования>\n"
                  << "Пример:  ./myfs_fuse ~/fs/myfs.img ~/mnt\n"
                  << "Отмонт.: fusermount3 -u ~/mnt\n";
        return 1;
    }

    g_fs = new MyFS();
    if (!g_fs->open(argv[1])) {
        std::cerr << "[ERR] Не могу открыть: " << argv[1] << "\n"
                  << "      Сначала: ./myfs " << argv[1] << " --format\n";
        return 1;
    }

    std::cout << "[OK] Sagittarius MyFS смонтирована: "
              << argv[1] << " -> " << argv[2] << "\n"
              << "     Ctrl+C или fusermount3 -u " << argv[2] << "\n";

    std::vector<char*> fuse_argv = { argv[0], argv[2] };
    char fg[] = "-f";
    char dio[] = "-odirect_io";
    fuse_argv.push_back(fg);
    fuse_argv.push_back(dio);
    return fuse_main((int)fuse_argv.size(), fuse_argv.data(), &sgs_ops, nullptr);
}