#include "../../include/bfs.h"
#include "../../include/memory.h"

#define BFS_SUPERBLOCK_LBA 1U

static const char *g_bfs_error = "sin error";

static bool bfs_fail(const char *error) {
    g_bfs_error = error;
    return false;
}

uint32_t bfs_metadata_crc32(const void *data, uint32_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t i = 0; i < size; i++) {
        crc ^= bytes[i];
        for (uint32_t bit = 0; bit < 8U; bit++)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

bool bfs_probe(block_device_t *device, bfs_kind_t *kind) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    uint32_t magic;
    if (kind) *kind = BFS_NONE;
    if (!device || device->sector_size != BLOCK_SECTOR_SIZE ||
        !block_read(device, BFS_SUPERBLOCK_LBA, 1U, sector))
        return bfs_fail("no se pudo leer el superbloque");
    magic = *(const uint32_t *)sector;
    if (magic == BFS16_MAGIC) {
        if (kind) *kind = BFS_16;
        return true;
    }
    if (magic == BFS32_MAGIC) {
        if (kind) *kind = BFS_32;
        return true;
    }
    return bfs_fail("firma BFS desconocida");
}

bool bfs_mount(block_device_t *device, bfs_fs_t *fs) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    bfs_kind_t kind;
    bfs32_superblock_t copy;
    uint32_t stored_crc;
    if (!fs || !bfs_probe(device, &kind)) return false;
    kmemset(fs, 0, sizeof(*fs));
    fs->kind = kind;
    fs->device = device;
    if (kind == BFS_16) {
        g_bfs_error = "sin error";
        return true; /* El adaptador BFS16 interpreta sus registros al leer. */
    }
    if (!block_read(device, BFS_SUPERBLOCK_LBA, 1U, sector))
        return bfs_fail("error leyendo BFS32");
    kmemcpy(&fs->super, sector, sizeof(fs->super));
    copy = fs->super;
    stored_crc = copy.metadata_crc32;
    copy.metadata_crc32 = 0U;
    if (copy.version != BFS32_VERSION ||
        copy.block_size != BLOCK_SECTOR_SIZE ||
        copy.total_blocks > device->sector_count ||
        copy.root_inode >= copy.inode_count)
        return bfs_fail("geometria BFS32 invalida");
    if (bfs_metadata_crc32(&copy, sizeof(copy)) != stored_crc)
        return bfs_fail("checksum del superbloque BFS32 invalido");
    g_bfs_error = copy.recovery_state == BFS32_STATE_DIRTY
                    ? "volumen BFS32 requiere recuperacion" : "sin error";
    return true;
}

bool bfs32_read_inode(const bfs_fs_t *fs, uint32_t index,
                      bfs32_inode_t *inode) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    uint32_t byte_offset, lba, within, stored_crc;
    bfs32_inode_t copy;
    if (!fs || fs->kind != BFS_32 || !inode ||
        index >= fs->super.inode_count)
        return bfs_fail("inodo BFS32 invalido");
    byte_offset = index * (uint32_t)sizeof(*inode);
    lba = fs->super.inode_start + byte_offset / BLOCK_SECTOR_SIZE;
    within = byte_offset % BLOCK_SECTOR_SIZE;
    if (within + sizeof(*inode) > BLOCK_SECTOR_SIZE)
        return bfs_fail("inodo BFS32 cruza un bloque");
    if (!block_read(fs->device, lba, 1U, sector))
        return bfs_fail("no se pudo leer el inodo BFS32");
    kmemcpy(inode, sector + within, sizeof(*inode));
    copy = *inode;
    stored_crc = copy.metadata_crc32;
    copy.metadata_crc32 = 0U;
    if (stored_crc && bfs_metadata_crc32(&copy, sizeof(copy)) != stored_crc)
        return bfs_fail("checksum de inodo BFS32 invalido");
    return true;
}

bool bfs32_read(const bfs_fs_t *fs, const bfs32_inode_t *inode,
                uint64_t offset, void *buffer, uint32_t size,
                uint32_t *bytes_read) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    uint8_t *out = (uint8_t *)buffer;
    uint64_t logical = 0U;
    uint32_t done = 0U;
    if (bytes_read) *bytes_read = 0U;
    if (!fs || fs->kind != BFS_32 || !inode || !buffer || !bytes_read)
        return bfs_fail("lectura BFS32 invalida");
    if (offset >= inode->size) return true;
    if ((uint64_t)size > inode->size - offset)
        size = (uint32_t)(inode->size - offset);
    for (uint32_t e = 0; e < inode->extent_count &&
                         e < BFS32_DIRECT_EXTENTS && done < size; e++) {
        uint64_t extent_bytes =
            (uint64_t)inode->direct[e].block_count * BLOCK_SECTOR_SIZE;
        if (offset >= logical + extent_bytes) {
            logical += extent_bytes;
            continue;
        }
        uint64_t pos = offset > logical ? offset - logical : 0U;
        while (pos < extent_bytes && done < size) {
            uint32_t lba = inode->direct[e].start_block +
                           (uint32_t)(pos / BLOCK_SECTOR_SIZE);
            uint32_t within = (uint32_t)(pos % BLOCK_SECTOR_SIZE);
            uint32_t chunk = BLOCK_SECTOR_SIZE - within;
            if (chunk > size - done) chunk = size - done;
            if (!block_read(fs->device, lba, 1U, sector))
                return bfs_fail("fallo leyendo extent BFS32");
            kmemcpy(out + done, sector + within, chunk);
            done += chunk;
            pos += chunk;
        }
        logical += extent_bytes;
    }
    *bytes_read = done;
    return done == size || bfs_fail("extents BFS32 incompletos");
}

const char *bfs_last_error(void) {
    return g_bfs_error;
}
