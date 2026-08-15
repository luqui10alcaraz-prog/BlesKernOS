#ifndef BFS_H
#define BFS_H

#include "types.h"
#include "block.h"

#define BFS32_MAGIC 0x32534642U /* "BFS2" little endian */
#define BFS16_MAGIC 0x31534642U /* "BFS1" little endian */
#define BFS32_VERSION 0x00010000U
#define BFS32_DIRECT_EXTENTS 8U
#define BFS32_NAME_MAX 127U

#define BFS_ATTR_READ_ONLY 0x00000001U
#define BFS_ATTR_HIDDEN    0x00000002U
#define BFS_ATTR_SYSTEM    0x00000004U
#define BFS_ATTR_DIRECTORY 0x00000010U
#define BFS_ATTR_ARCHIVE   0x00000020U

#define BFS32_STATE_CLEAN 0x434C454EU
#define BFS32_STATE_DIRTY 0x44495254U

typedef enum {
    BFS_NONE = 0,
    BFS_16,
    BFS_32
} bfs_kind_t;

typedef struct PACKED {
    uint32_t start_block;
    uint32_t block_count;
} bfs32_extent_t;

typedef struct PACKED {
    uint32_t id;
    uint32_t parent_id;
    uint64_t size;
    uint32_t attributes;
    uint32_t owner;
    uint64_t created;
    uint64_t modified;
    uint64_t accessed;
    uint32_t extent_count;
    bfs32_extent_t direct[BFS32_DIRECT_EXTENTS];
    uint32_t indirect_extent_block;
    uint32_t generation;
    uint32_t metadata_crc32;
    char name[BFS32_NAME_MAX + 1U];
} bfs32_inode_t;

typedef struct PACKED {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t bitmap_start;
    uint32_t bitmap_blocks;
    uint32_t inode_start;
    uint32_t inode_blocks;
    uint32_t inode_count;
    uint32_t root_inode;
    uint32_t next_file_id;
    uint32_t recovery_state;
    uint32_t sequence;
    uint64_t created;
    char volume_name[32];
    uint32_t metadata_crc32;
} bfs32_superblock_t;

typedef struct {
    bfs_kind_t kind;
    block_device_t *device;
    bfs32_superblock_t super;
} bfs_fs_t;

bool bfs_probe(block_device_t *device, bfs_kind_t *kind);
bool bfs_mount(block_device_t *device, bfs_fs_t *fs);
bool bfs32_read_inode(const bfs_fs_t *fs, uint32_t index,
                      bfs32_inode_t *inode);
bool bfs32_read(const bfs_fs_t *fs, const bfs32_inode_t *inode,
                uint64_t offset, void *buffer, uint32_t size,
                uint32_t *bytes_read);
uint32_t bfs_metadata_crc32(const void *data, uint32_t size);
const char *bfs_last_error(void);

#endif
