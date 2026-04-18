#pragma once
// Sagittarius SGS-1 -- MyFS v0.2
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>

constexpr uint32_t MYFS_MAGIC        = 0x4D594602;
constexpr uint32_t MYFS_BLOCK_SIZE   = 4096;
constexpr uint32_t MYFS_TOTAL_BLOCKS = 4096;
constexpr uint32_t MYFS_MAX_NAME     = 64;
constexpr uint32_t MYFS_MAX_DIRECT   = 12;
constexpr uint32_t MYFS_NULL_BLOCK   = 0xFFFFFFFF;

constexpr uint8_t COMPRESS_NONE = 0;
constexpr uint8_t COMPRESS_ZSTD = 1;
constexpr int     ZSTD_LEVEL    = 1; // быстро, lossless, малая нагрузка

enum class TxState : uint32_t {
    CLEAN     = 0x00000000,
    PENDING   = 0xDEADBEEF,
    COMMITTED = 0xC0FFEE00,
};

struct SuperBlock {
    uint32_t magic;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t inode_table_start;
    uint32_t inode_count;
    uint32_t data_start;
    uint32_t root_inode;
    uint32_t generation;
    TxState  tx_state;
    uint32_t tx_old_block;
    uint32_t tx_new_block;
    uint32_t tx_inode_idx;
    uint32_t compressed_blocks;
    uint32_t saved_bytes;
    char     label[32];
    uint8_t  _pad[MYFS_BLOCK_SIZE - 15*4 - 32];
};
static_assert(sizeof(SuperBlock) == MYFS_BLOCK_SIZE);

struct BlockBitmap {
    uint8_t bits[MYFS_BLOCK_SIZE];
};

struct Inode {
    uint8_t  used;
    uint8_t  type;          // 0=файл 1=директория
    uint8_t  cow_pending;
    uint8_t  compression;   // COMPRESS_NONE / COMPRESS_ZSTD
    uint32_t size;          // реальный размер
    uint32_t size_on_disk;  // размер на диске (после сжатия)
    uint32_t blocks_used;
    uint32_t generation;
    char     name[MYFS_MAX_NAME];
    char     parent_path[112];
    uint32_t direct[MYFS_MAX_DIRECT];
    uint32_t cow_shadow[MYFS_MAX_DIRECT];
};

class MyFS {
public:
    bool        open      (const std::string& path, bool format = false);
    void        close     ();
    bool        createFile(const std::string& path, const std::string& content = "");
    bool        writeFile (const std::string& path, const std::string& content);
    std::string readFile  (const std::string& path);
    bool        createDir (const std::string& path);
    bool        remove    (const std::string& path);
    void        compact   ();
    void        printMap  ();
    void        info      ();
    void        ls        (const std::string& path = "/");

    // FUSE
    struct StatInfo { bool exists, is_dir; uint32_t size, generation; };
    StatInfo           stat           (const std::string& path);
    std::vector<Inode> listDir        (const std::string& path);
    std::string        readFileByInode(const Inode& node);

    // Для fuse write
    bool mkdirFuse (const std::string& path);
    bool createFuse(const std::string& path);
    int  writeFuse (const std::string& path, const char* buf, size_t size, off_t offset);
    bool unlinkFuse(const std::string& path);
    bool truncFuse (const std::string& path, off_t size);

private:
    std::fstream disk_;
    SuperBlock   sb_;
    BlockBitmap  bitmap_;

    static constexpr uint32_t INODE_TABLE_BLOCK  = 2;
    static constexpr uint32_t INODE_TABLE_BLOCKS = 16;
    static constexpr uint32_t INODES_PER_BLOCK   = MYFS_BLOCK_SIZE / sizeof(Inode);
    static constexpr uint32_t DATA_START         = INODE_TABLE_BLOCK + INODE_TABLE_BLOCKS;

    bool    formatDisk();
    bool    mountDisk();
    void    recoverIfNeeded();
    void    setBit(uint32_t b, bool v);
    bool    getBit(uint32_t b) const;
    int32_t findExtent(uint32_t needed);
    int32_t allocInode();
    Inode   readInode (uint32_t idx);
    void    writeInode(uint32_t idx, const Inode& node);
    void    writeRawBlock(uint32_t block, const void* data, size_t len);
    void    writeSuperBlock();
    void    writeBitmap();
    int32_t inodeByPath(const std::string& path);
    bool    dirExists  (const std::string& path);
    std::pair<std::string,std::string> splitPath(const std::string& path);
    uint32_t countFragments();

    std::vector<uint8_t> compressData  (const std::string& data, bool& did_compress);
    std::string          decompressData(const uint8_t* data, size_t csize, size_t orig_size);
    bool writeFileInternal(int32_t idx, Inode& node, const std::string& content);

    void log(const std::string& level, const std::string& msg);
};