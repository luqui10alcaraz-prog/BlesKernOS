#include "include/graphics_resources.h"
#include "include/memory.h"
#include "include/vfs.h"
#include "include/klock.h"
#include "include/vga.h"
#include "include/compat_mode.h"
#include "../gui/image.h"
#include "string.h"

#define BKGP_HEADER_SIZE 12U
#define BKGP_ENTRY_SIZE 64U
#define BKGP_NAME_SIZE 48U
#define BKGP_MAX_ENTRIES 4096U
#define BKGP_DECODE_CACHE_MAX 96U
#define BKGP_DECODE_CACHE_PIXELS_MAX (512U * 1024U)
#define BKGP_LOW_CACHE_MAX 24U
#define BKGP_LOW_CACHE_PIXELS_MAX (64U * 1024U)

typedef struct {
    char name[BKGP_NAME_SIZE];
    gui_image_t image;
} graphics_cache_entry_t;

static void *g_graphics_pak;
static uint32_t g_graphics_pak_size;
static uint32_t g_graphics_pak_count;
static bool g_graphics_pak_attempted;
static bool g_graphics_pak_table_only;
static graphics_cache_entry_t g_graphics_cache[BKGP_DECODE_CACHE_MAX];
static uint32_t g_graphics_cache_count;
static uint32_t g_graphics_cache_pixels;
static kmutex_t g_graphics_lock = KMUTEX_INITIALIZER;

static bool graphics_read_range(uint32_t offset, void *buffer,
                                uint32_t size) {
    int fd;
    int got;
    if (!buffer || !size || offset > 0x7FFFFFFFU) return false;
    fd = vfs_open(BK_GRAPHICS_PAK_PATH, VFS_O_RDONLY);
    if (fd < 0) return false;
    if (vfs_seek(fd, (int32_t)offset, 0U) != (int32_t)offset) {
        vfs_close(fd);
        return false;
    }
    got = vfs_read(fd, buffer, size);
    vfs_close(fd);
    return got >= 0 && (uint32_t)got == size;
}

static uint16_t graphics_read_u16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t graphics_read_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static bool graphics_ascii_equal_ci(const uint8_t *packed,
                                    const char *requested) {
    uint32_t index = 0U;
    if (!packed || !requested) return false;
    while (index < BKGP_NAME_SIZE && packed[index] && requested[index]) {
        char left = (char)packed[index];
        char right = requested[index];
        if (left >= 'A' && left <= 'Z') left = (char)(left - 'A' + 'a');
        if (right >= 'A' && right <= 'Z') right = (char)(right - 'A' + 'a');
        if (left != right) return false;
        index++;
    }
    return index < BKGP_NAME_SIZE && packed[index] == 0 &&
           requested[index] == 0;
}

static bool graphics_load_package(void) {
    uint8_t *bytes;
    uint32_t count;
    uint32_t table_size;
    vfs_dir_entry_t entry;
    if (g_graphics_pak) return true;
    if (g_graphics_pak_attempted) return false;
    g_graphics_pak_attempted = true;
    if (compat_mode_is_low_memory()) {
        uint8_t header[BKGP_HEADER_SIZE];
        if (!vfs_stat(BK_GRAPHICS_PAK_PATH, &entry) ||
            entry.type != VFS_NODE_FILE || entry.size < BKGP_HEADER_SIZE ||
            !graphics_read_range(0U, header, sizeof(header))) return false;
        count = graphics_read_u32(header + 8U);
        if (header[0] != 'B' || header[1] != 'K' ||
            header[2] != 'G' || header[3] != 'P' ||
            graphics_read_u32(header + 4U) != 1U || !count ||
            count > BKGP_MAX_ENTRIES) return false;
        table_size = BKGP_HEADER_SIZE + count * BKGP_ENTRY_SIZE;
        if (table_size > entry.size) return false;
        g_graphics_pak = kmalloc(table_size);
        if (!g_graphics_pak ||
            !graphics_read_range(0U, g_graphics_pak, table_size)) {
            if (g_graphics_pak) kfree(g_graphics_pak);
            g_graphics_pak = NULL;
            return false;
        }
        g_graphics_pak_size = entry.size;
        g_graphics_pak_table_only = true;
    } else if (!vfs_read_all(BK_GRAPHICS_PAK_PATH, &g_graphics_pak,
                             &g_graphics_pak_size) || !g_graphics_pak) {
        return false;
    }
    /* El paquete queda en una cache global y no pertenece al proceso que
     * hizo la primera consulta. */
    if (!mm_set_allocation_owner(g_graphics_pak, 0U)) {
        kfree(g_graphics_pak);
        g_graphics_pak = NULL;
        g_graphics_pak_size = 0U;
        return false;
    }
    bytes = (uint8_t *)g_graphics_pak;
    if (g_graphics_pak_size < BKGP_HEADER_SIZE ||
        bytes[0] != 'B' || bytes[1] != 'K' ||
        bytes[2] != 'G' || bytes[3] != 'P' ||
        graphics_read_u32(bytes + 4U) != 1U) goto invalid;
    count = graphics_read_u32(bytes + 8U);
    if (!count || count > BKGP_MAX_ENTRIES) goto invalid;
    table_size = BKGP_HEADER_SIZE + count * BKGP_ENTRY_SIZE;
    if (table_size > g_graphics_pak_size) goto invalid;
    g_graphics_pak_count = count;
    return true;

invalid:
    kfree(g_graphics_pak);
    g_graphics_pak = NULL;
    g_graphics_pak_size = 0U;
    g_graphics_pak_table_only = false;
    return false;
}

static const uint8_t *graphics_entry(uint32_t index) {
    if (!graphics_load_package() || index >= g_graphics_pak_count) return NULL;
    return (const uint8_t *)g_graphics_pak + BKGP_HEADER_SIZE +
           index * BKGP_ENTRY_SIZE;
}

static bool graphics_clone_image(const gui_image_t *source,
                                 gui_image_t *destination) {
    uint32_t pixels;
    if (!source || !source->pixels || !source->width || !source->height ||
        !destination) return false;
    pixels = (uint32_t)source->width * source->height;
    destination->pixels = (uint32_t *)kmalloc(
        (size_t)pixels * sizeof(uint32_t));
    if (!destination->pixels) return false;
    kmemcpy(destination->pixels, source->pixels,
            (size_t)pixels * sizeof(uint32_t));
    destination->width = source->width;
    destination->height = source->height;
    return true;
}

static bool graphics_icon_load_locked(const char *name, gui_image_t *image) {
    uint32_t cache_max = compat_mode_is_low_memory()
        ? BKGP_LOW_CACHE_MAX : BKGP_DECODE_CACHE_MAX;
    uint32_t cache_pixels_max = compat_mode_is_low_memory()
        ? BKGP_LOW_CACHE_PIXELS_MAX : BKGP_DECODE_CACHE_PIXELS_MAX;
    if (!name || !name[0] || !image) return false;
    kmemset(image, 0, sizeof(*image));
    for (uint32_t index = 0U; index < g_graphics_cache_count; index++) {
        if (graphics_ascii_equal_ci(
                (const uint8_t *)g_graphics_cache[index].name, name))
            return graphics_clone_image(&g_graphics_cache[index].image,
                                        image);
    }
    if (!graphics_load_package()) return false;
    for (uint32_t index = 0U; index < g_graphics_pak_count; index++) {
        const uint8_t *entry = graphics_entry(index);
        uint16_t type;
        uint32_t offset;
        uint32_t size;
        uint8_t *owned_payload = NULL;
        const uint8_t *payload;
        if (!entry || !graphics_ascii_equal_ci(entry, name)) continue;
        type = graphics_read_u16(entry + 52U);
        offset = graphics_read_u32(entry + 56U);
        size = graphics_read_u32(entry + 60U);
        if (type != 1U || !size || offset > g_graphics_pak_size ||
            size > g_graphics_pak_size - offset) return false;
        if (g_graphics_pak_table_only) {
            owned_payload = (uint8_t *)kmalloc(size);
            if (!owned_payload ||
                !graphics_read_range(offset, owned_payload, size)) {
                if (owned_payload) kfree(owned_payload);
                return false;
            }
            payload = owned_payload;
        } else {
            payload = (const uint8_t *)g_graphics_pak + offset;
        }
        if (g_graphics_cache_count < cache_max) {
            graphics_cache_entry_t *cached =
                &g_graphics_cache[g_graphics_cache_count];
            if (!gui_png_decode(&cached->image, payload, size)) {
                if (owned_payload) kfree(owned_payload);
                return false;
            }
            if (owned_payload) kfree(owned_payload);
            if (!mm_set_allocation_owner(cached->image.pixels, 0U)) {
                gui_image_free(&cached->image);
                return false;
            }
            {
                uint32_t decoded_pixels =
                    (uint32_t)cached->image.width * cached->image.height;
                if (decoded_pixels >
                    cache_pixels_max - g_graphics_cache_pixels) {
                    *image = cached->image;
                    kmemset(cached, 0, sizeof(*cached));
                    return true;
                }
                g_graphics_cache_pixels += decoded_pixels;
            }
            kstrncpy(cached->name, name, sizeof(cached->name) - 1U);
            cached->name[sizeof(cached->name) - 1U] = '\0';
            g_graphics_cache_count++;
            return graphics_clone_image(&cached->image, image);
        }
        {
            bool decoded = gui_png_decode(image, payload, size);
            if (owned_payload) kfree(owned_payload);
            return decoded;
        }
    }
    return false;
}

bool bk_graphics_icon_load(const char *name, gui_image_t *image) {
    bool result;
    kmutex_lock(&g_graphics_lock);
    result = graphics_icon_load_locked(name, image);
    kmutex_unlock(&g_graphics_lock);
    return result;
}

uint32_t bk_graphics_icon_count(void) {
    uint32_t count;
    kmutex_lock(&g_graphics_lock);
    count = graphics_load_package() ? g_graphics_pak_count : 0U;
    kmutex_unlock(&g_graphics_lock);
    return count;
}

bool bk_graphics_icon_name(uint32_t index, char *buffer, uint32_t capacity) {
    const uint8_t *entry;
    uint32_t length = 0U;
    bool result = false;
    kmutex_lock(&g_graphics_lock);
    entry = graphics_entry(index);
    if (entry && buffer && capacity) {
        while (length < BKGP_NAME_SIZE && entry[length]) length++;
        if (length < capacity) {
            for (uint32_t i = 0U; i < length; i++)
                buffer[i] = (char)entry[i];
            buffer[length] = '\0';
            result = true;
        }
    }
    kmutex_unlock(&g_graphics_lock);
    return result;
}

bool bk_graphics_preload_boot_icons(void) {
    static const char *const names[] = {
        "InfoBubble", "Runonce106", "Computer", "FolderExe",
        "FileText", "Folder", "Settings", "PowerOff", "Appwiz1500"
    };
    uint32_t loaded = 0U;

    for (uint32_t i = 0U; i < sizeof(names) / sizeof(names[0]); i++) {
        gui_image_t image = {0};
        if (bk_graphics_icon_load(names[i], &image)) {
            loaded++;
            gui_image_free(&image);
        } else {
            kprintf("[GFXRES] preload FAIL %s\n", names[i]);
        }
    }
    kprintf("[GFXRES] boot icons %u/%u precargados\n", loaded,
            (uint32_t)(sizeof(names) / sizeof(names[0])));
    return loaded == (uint32_t)(sizeof(names) / sizeof(names[0]));
}
