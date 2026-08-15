#include "resources.h"
#include "../include/pe_loader.h"
#include "../include/task.h"
#include "../include/memory.h"
#include "../stdio.h"

#define RESOURCE_HANDLE_BASE 0x77000000U
/* BLES_WINE_RESOURCE_HANDLE_CACHE_20260723
 *
 * FindResource returns stable pseudo-handles. The old 48-slot table created
 * a fresh slot for every lookup, so repeated LoadString calls exhausted it
 * and WinZip recursively reported the resource failure until its stack died.
 */
#define RESOURCE_HANDLE_MAX  512U
#define RESOURCE_HANDLE_LOG_STEP 64U
#define RESOURCE_DIRECTORY_FLAG 0x80000000U
#define RESOURCE_NAME_FLAG      0x80000000U

#define ERROR_RESOURCE_DATA_NOT_FOUND 1812U
#define ERROR_RESOURCE_TYPE_NOT_FOUND 1813U
#define ERROR_RESOURCE_NAME_NOT_FOUND 1814U
#define ERROR_RESOURCE_LANG_NOT_FOUND 1815U
#define ERROR_INVALID_HANDLE 6U

#define ICON_HANDLE_BASE 0x7A000000U
#define ICON_HANDLE_MAX  48U

typedef struct {
    bool used;
    bool shared;
    uint32_t pid;
    int width;
    int height;
    uint32_t *pixels;
} icon_handle_t;

typedef struct {
    uint32_t characteristics;
    uint32_t timestamp;
    uint16_t major_version;
    uint16_t minor_version;
    uint16_t named_entries;
    uint16_t id_entries;
} PACKED resource_directory_t;

typedef struct {
    uint32_t name;
    uint32_t offset;
} PACKED resource_directory_entry_t;

typedef struct {
    uint32_t data_rva;
    uint32_t size;
    uint32_t codepage;
    uint32_t reserved;
} PACKED resource_data_entry_t;

typedef struct {
    bool used;
    uint32_t pid;
    void *module;
    const uint8_t *data;
    uint32_t size;
    uint16_t language;
} resource_handle_t;

static resource_handle_t resource_handles[RESOURCE_HANDLE_MAX];
static icon_handle_t icon_handles[ICON_HANDLE_MAX];

static uint16_t icon_read16(const uint8_t *data) {
    return (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t icon_read32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static icon_handle_t *icon_from_handle(void *handle) {
    uint32_t value = (uint32_t)(uintptr_t)handle;
    if (value < ICON_HANDLE_BASE ||
        value >= ICON_HANDLE_BASE + ICON_HANDLE_MAX) return NULL;
    value -= ICON_HANDLE_BASE;
    return icon_handles[value].used ? &icon_handles[value] : NULL;
}

static void *icon_allocate(int width, int height, bool shared) {
    if (width <= 0 || height <= 0 || width > 256 || height > 256) return NULL;
    for (uint32_t i = 0; i < ICON_HANDLE_MAX; i++) {
        icon_handle_t *icon = &icon_handles[i];
        if (icon->used) continue;
        icon->pixels = (uint32_t *)kzalloc((size_t)width * (size_t)height *
                                           sizeof(uint32_t));
        if (!icon->pixels) return NULL;
        icon->used = true;
        icon->shared = shared;
        icon->pid = task_current_process_id();
        icon->width = width;
        icon->height = height;
        return (void *)(uintptr_t)(ICON_HANDLE_BASE + i);
    }
    return NULL;
}

/* RT_ICON contiene un DIB sin cabecera de archivo. biHeight incluye las
 * mitades XOR y AND. Esta es la misma separacion usada por cursoricon.c de
 * Wine para iconos clasicos de Windows 3.x/9x. */
void *win32_icon_create_from_resource(const void *raw, uint32_t size,
                                      int requested_width UNUSED,
                                      int requested_height UNUSED) {
    const uint8_t *data = (const uint8_t *)raw;
    uint32_t header_size, compression, colors, palette_bytes;
    uint32_t xor_offset, xor_stride, and_offset, and_stride;
    int width, stored_height, height;
    uint16_t planes, bpp;
    void *handle;
    icon_handle_t *icon;
    bool alpha_present = false;

    if (!data || size < 40U) return NULL;
    header_size = icon_read32(data);
    if (header_size < 40U || header_size > size) return NULL;
    width = (int32_t)icon_read32(data + 4U);
    stored_height = (int32_t)icon_read32(data + 8U);
    planes = icon_read16(data + 12U);
    bpp = icon_read16(data + 14U);
    compression = icon_read32(data + 16U);
    colors = icon_read32(data + 32U);
    if (width <= 0 || stored_height <= 0 || (stored_height & 1) || planes != 1U ||
        compression != 0U || (bpp != 1U && bpp != 4U && bpp != 8U &&
                               bpp != 24U && bpp != 32U)) return NULL;
    height = stored_height / 2;
    if (!colors && bpp <= 8U) colors = 1U << bpp;
    palette_bytes = colors * 4U;
    if (header_size > size || palette_bytes > size - header_size) return NULL;
    xor_offset = header_size + palette_bytes;
    xor_stride = (((uint32_t)width * bpp + 31U) / 32U) * 4U;
    if ((uint32_t)height > (size - xor_offset) / xor_stride) return NULL;
    and_offset = xor_offset + xor_stride * (uint32_t)height;
    and_stride = (((uint32_t)width + 31U) / 32U) * 4U;
    if ((uint32_t)height > (size - and_offset) / and_stride) return NULL;

    handle = icon_allocate(width, height, false);
    icon = icon_from_handle(handle);
    if (!icon) return NULL;

    if (bpp == 32U) {
        for (int y = 0; y < height && !alpha_present; y++) {
            const uint8_t *row = data + xor_offset +
                                 (uint32_t)(height - 1 - y) * xor_stride;
            for (int x = 0; x < width; x++)
                if (row[x * 4 + 3]) { alpha_present = true; break; }
        }
    }

    for (int y = 0; y < height; y++) {
        const uint8_t *xor_row = data + xor_offset +
                                 (uint32_t)(height - 1 - y) * xor_stride;
        const uint8_t *and_row = data + and_offset +
                                 (uint32_t)(height - 1 - y) * and_stride;
        for (int x = 0; x < width; x++) {
            uint8_t index = 0U, alpha = 0xFFU;
            uint32_t color;
            if (bpp == 32U) {
                const uint8_t *pixel = xor_row + x * 4;
                color = (uint32_t)pixel[2] << 16 |
                        (uint32_t)pixel[1] << 8 | pixel[0];
                if (alpha_present) alpha = pixel[3];
            } else if (bpp == 24U) {
                const uint8_t *pixel = xor_row + x * 3;
                color = (uint32_t)pixel[2] << 16 |
                        (uint32_t)pixel[1] << 8 | pixel[0];
            } else {
                if (bpp == 8U) index = xor_row[x];
                else if (bpp == 4U) {
                    uint8_t pair = xor_row[x >> 1];
                    index = (x & 1) ? (pair & 0x0FU) : (pair >> 4);
                } else index = (xor_row[x >> 3] >> (7 - (x & 7))) & 1U;
                if (index >= colors) index = 0U;
                const uint8_t *entry = data + header_size + index * 4U;
                color = (uint32_t)entry[2] << 16 |
                        (uint32_t)entry[1] << 8 | entry[0];
            }
            if (!alpha_present &&
                ((and_row[x >> 3] >> (7 - (x & 7))) & 1U)) alpha = 0U;
            icon->pixels[(uint32_t)y * (uint32_t)width + (uint32_t)x] =
                ((uint32_t)alpha << 24) | color;
        }
    }
    return handle;
}

static bool pointer_is_id(const void *value) {
    return (uint32_t)(uintptr_t)value <= 0xFFFFU;
}

static uint8_t ascii_upper(uint8_t c) {
    return c >= 'a' && c <= 'z' ? (uint8_t)(c - ('a' - 'A')) : c;
}

static bool resource_name_matches(const uint8_t *root, uint32_t root_size,
                                  uint32_t encoded, const void *wanted,
                                  bool wanted_wide) {
    if (pointer_is_id(wanted)) {
        return (encoded & RESOURCE_NAME_FLAG) == 0U &&
               (encoded & 0xFFFFU) == (uint32_t)(uintptr_t)wanted;
    }
    if ((encoded & RESOURCE_NAME_FLAG) == 0U) return false;

    uint32_t offset = encoded & ~RESOURCE_NAME_FLAG;
    if (offset > root_size || root_size - offset < sizeof(uint16_t)) return false;
    const uint16_t *name = (const uint16_t *)(root + offset);
    uint16_t length = name[0];
    if ((uint32_t)length * sizeof(uint16_t) > root_size - offset - sizeof(uint16_t))
        return false;
    name++;

    if (wanted_wide) {
        const uint16_t *text = (const uint16_t *)wanted;
        uint32_t i = 0;
        while (i < length && text[i]) {
            uint16_t a = name[i], b = text[i];
            if (a <= 0x7FU) a = ascii_upper((uint8_t)a);
            if (b <= 0x7FU) b = ascii_upper((uint8_t)b);
            if (a != b) return false;
            i++;
        }
        return i == length && text[i] == 0U;
    }

    const char *text = (const char *)wanted;
    uint32_t i = 0;
    while (i < length && text[i]) {
        uint16_t a = name[i];
        uint8_t b = ascii_upper((uint8_t)text[i]);
        if (a > 0xFFU || ascii_upper((uint8_t)a) != b) return false;
        i++;
    }
    return i == length && text[i] == '\0';
}

static const resource_directory_entry_t *find_entry(
        const uint8_t *root, uint32_t root_size, uint32_t directory_offset,
        const void *wanted, bool wanted_wide, bool allow_first,
        uint16_t *id_out) {
    if (directory_offset > root_size ||
        root_size - directory_offset < sizeof(resource_directory_t)) return NULL;
    const resource_directory_t *directory =
        (const resource_directory_t *)(root + directory_offset);
    uint32_t count = (uint32_t)directory->named_entries + directory->id_entries;
    uint32_t bytes = count * sizeof(resource_directory_entry_t);
    if (bytes > root_size - directory_offset - sizeof(*directory)) return NULL;
    const resource_directory_entry_t *entries =
        (const resource_directory_entry_t *)(directory + 1);

    if (allow_first && count) {
        if (id_out) *id_out = (uint16_t)(entries[0].name & 0xFFFFU);
        return &entries[0];
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!resource_name_matches(root, root_size, entries[i].name,
                                   wanted, wanted_wide)) continue;
        if (id_out) *id_out = (uint16_t)(entries[i].name & 0xFFFFU);
        return &entries[i];
    }
    return NULL;
}

static resource_handle_t *handle_from_value(void *handle) {
    uint32_t value = (uint32_t)(uintptr_t)handle;
    if (value < RESOURCE_HANDLE_BASE ||
        value >= RESOURCE_HANDLE_BASE + RESOURCE_HANDLE_MAX) return NULL;
    value -= RESOURCE_HANDLE_BASE;
    return resource_handles[value].used ? &resource_handles[value] : NULL;
}

static void *allocate_handle(void *module, const uint8_t *data,
                             uint32_t size, uint16_t language) {
    uint32_t pid = task_current_process_id();
    uint32_t free_slot = RESOURCE_HANDLE_MAX;
    uint32_t used_by_process = 0U;
    void *result = NULL;
    bool reused = false;

    task_preempt_disable();

    for (uint32_t i = 0; i < RESOURCE_HANDLE_MAX; i++) {
        resource_handle_t *entry = &resource_handles[i];

        if (!entry->used) {
            if (free_slot == RESOURCE_HANDLE_MAX) free_slot = i;
            continue;
        }

        if (entry->pid == pid) used_by_process++;

        if (entry->pid == pid &&
            entry->module == module &&
            entry->data == data &&
            entry->size == size &&
            entry->language == language) {
            result = (void *)(uintptr_t)(RESOURCE_HANDLE_BASE + i);
            reused = true;
            break;
        }
    }

    if (!result && free_slot < RESOURCE_HANDLE_MAX) {
        resource_handle_t *entry = &resource_handles[free_slot];

        entry->used = true;
        entry->pid = pid;
        entry->module = module;
        entry->data = data;
        entry->size = size;
        entry->language = language;
        result = (void *)(uintptr_t)(RESOURCE_HANDLE_BASE + free_slot);
        used_by_process++;
    }

    task_preempt_enable();

    if (!result) {
        kprintf("[RES:HANDLE] FULL pid=%u capacity=%u module=%x "
                "data=%x size=%u lang=%u\n",
                pid, RESOURCE_HANDLE_MAX,
                (uint32_t)(uintptr_t)module,
                (uint32_t)(uintptr_t)data,
                size, language);
        return NULL;
    }

    if (!reused &&
        (used_by_process == 1U ||
         (used_by_process % RESOURCE_HANDLE_LOG_STEP) == 0U ||
         used_by_process + 16U >= RESOURCE_HANDLE_MAX)) {
        kprintf("[RES:HANDLE] NEW pid=%u handle=%x used=%u/%u "
                "module=%x size=%u lang=%u\n",
                pid, (uint32_t)(uintptr_t)result,
                used_by_process, RESOURCE_HANDLE_MAX,
                (uint32_t)(uintptr_t)module,
                size, language);
    }

    return result;
}

static void *resource_find_internal(void *module, const void *type,
                                    const void *name, uint16_t language,
                                    bool exact_language, bool wide_names) {
    const uint8_t *image = NULL;
    uint32_t image_size = 0, resource_rva = 0, resource_size = 0;
    const resource_directory_entry_t *entry;
    const resource_data_entry_t *data_entry;
    uint16_t selected_language = 0;

    if (!pe_win32_get_image_resource(module, &image, &image_size,
                                     &resource_rva, &resource_size) ||
        !resource_rva || !resource_size ||
        resource_rva > image_size || resource_size > image_size - resource_rva) {
        pe_win32_set_last_error(ERROR_RESOURCE_DATA_NOT_FOUND);
        return NULL;
    }
    const uint8_t *root = image + resource_rva;

    entry = find_entry(root, resource_size, 0U, type, wide_names, false, NULL);
    if (!entry || (entry->offset & RESOURCE_DIRECTORY_FLAG) == 0U) {
        pe_win32_set_last_error(ERROR_RESOURCE_TYPE_NOT_FOUND);
        return NULL;
    }
    uint32_t type_dir = entry->offset & ~RESOURCE_DIRECTORY_FLAG;
    entry = find_entry(root, resource_size, type_dir, name, wide_names, false, NULL);
    if (!entry || (entry->offset & RESOURCE_DIRECTORY_FLAG) == 0U) {
        pe_win32_set_last_error(ERROR_RESOURCE_NAME_NOT_FOUND);
        return NULL;
    }
    uint32_t name_dir = entry->offset & ~RESOURCE_DIRECTORY_FLAG;
    if (exact_language) {
        entry = find_entry(root, resource_size, name_dir,
                           (const void *)(uintptr_t)language,
                           false, false, &selected_language);
    } else {
        entry = find_entry(root, resource_size, name_dir,
                           NULL, false, true, &selected_language);
    }
    if (!entry || (entry->offset & RESOURCE_DIRECTORY_FLAG) != 0U) {
        pe_win32_set_last_error(ERROR_RESOURCE_LANG_NOT_FOUND);
        return NULL;
    }
    uint32_t data_offset = entry->offset;
    if (data_offset > resource_size ||
        resource_size - data_offset < sizeof(resource_data_entry_t)) {
        pe_win32_set_last_error(ERROR_RESOURCE_DATA_NOT_FOUND);
        return NULL;
    }
    data_entry = (const resource_data_entry_t *)(root + data_offset);
    if (data_entry->data_rva > image_size ||
        data_entry->size > image_size - data_entry->data_rva) {
        pe_win32_set_last_error(ERROR_RESOURCE_DATA_NOT_FOUND);
        return NULL;
    }
    void *result = allocate_handle(module ? module : (void *)image,
                                   image + data_entry->data_rva,
                                   data_entry->size, selected_language);
    pe_win32_set_last_error(result ? 0U : ERROR_INVALID_HANDLE);
    return result;
}

void *win32_resource_find(void *module, const void *type, const void *name,
                          uint16_t language, bool exact_language) {
    return resource_find_internal(module, type, name, language,
                                  exact_language, false);
}

void *win32_resource_find_w(void *module, const void *type, const void *name,
                            uint16_t language, bool exact_language) {
    return resource_find_internal(module, type, name, language,
                                  exact_language, true);
}

void *win32_resource_load(void *module UNUSED, void *resource) {
    return handle_from_value(resource) ? resource : NULL;
}

const void *win32_resource_lock(void *loaded_resource) {
    resource_handle_t *resource = handle_from_value(loaded_resource);
    return resource ? resource->data : NULL;
}

uint32_t win32_resource_size(void *module UNUSED, void *resource) {
    resource_handle_t *entry = handle_from_value(resource);
    return entry ? entry->size : 0U;
}

bool win32_resource_free(void *loaded_resource) {
    resource_handle_t *resource = handle_from_value(loaded_resource);
    if (!resource) return false;
    /* Win32 moderno trata FreeResource como no-op. Conservamos el handle. */
    return true;
}

static void *icon_create_system(uint32_t id, int width, int height) {
    void *handle;
    icon_handle_t *icon;
    uint32_t main_color = id == 32513U ? 0x00D03030U :
                          id == 32514U ? 0x00E0B020U :
                          id == 32515U ? 0x003080D0U :
                          id == 32516U ? 0x00D03030U : 0x004080D0U;
    if (width <= 0) width = 32;
    if (height <= 0) height = 32;
    handle = icon_allocate(width, height, true);
    icon = icon_from_handle(handle);
    if (!icon) return NULL;
    for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
        int border = width < height ? width / 8 : height / 8;
        bool inside = x >= border && y >= border &&
                      x < width - border && y < height - border;
        if (!inside) icon->pixels[y * width + x] = 0U;
        else if (x == border || y == border ||
                 x == width - border - 1 || y == height - border - 1)
            icon->pixels[y * width + x] = 0xFF202020U;
        else icon->pixels[y * width + x] = 0xFF000000U | main_color;
    }
    return handle;
}

void *win32_icon_load(void *module, const void *name, bool wide_name,
                      int requested_width, int requested_height) {
    void *group_resource, *icon_resource;
    const uint8_t *group;
    uint32_t group_size;
    uint16_t count, selected = 0U, icon_id;
    uint32_t best_difference = 0xFFFFFFFFU;

    if (!module && pointer_is_id(name))
        return icon_create_system((uint32_t)(uintptr_t)name,
                                  requested_width, requested_height);
    group_resource = wide_name
        ? win32_resource_find_w(module,
            (const void *)(uintptr_t)WIN32_RT_GROUP_ICON, name, 0U, false)
        : win32_resource_find(module,
            (const void *)(uintptr_t)WIN32_RT_GROUP_ICON, name, 0U, false);
    group = (const uint8_t *)win32_resource_lock(group_resource);
    group_size = win32_resource_size(module, group_resource);
    if (!group || group_size < 6U || icon_read16(group) != 0U ||
        icon_read16(group + 2U) != 1U) return NULL;
    count = icon_read16(group + 4U);
    if (!count || count > (group_size - 6U) / 14U) return NULL;
    if (requested_width <= 0) requested_width = 32;
    if (requested_height <= 0) requested_height = 32;
    for (uint16_t i = 0; i < count; i++) {
        const uint8_t *entry = group + 6U + (uint32_t)i * 14U;
        int width = entry[0] ? entry[0] : 256;
        int height = entry[1] ? entry[1] : 256;
        uint32_t difference = (uint32_t)(width > requested_width ?
            width - requested_width : requested_width - width) +
            (uint32_t)(height > requested_height ?
            height - requested_height : requested_height - height);
        if (difference < best_difference) {
            best_difference = difference;
            selected = i;
        }
    }
    icon_id = icon_read16(group + 6U + (uint32_t)selected * 14U + 12U);
    icon_resource = win32_resource_find(module,
        (const void *)(uintptr_t)WIN32_RT_ICON,
        (const void *)(uintptr_t)icon_id, 0U, false);
    return win32_icon_create_from_resource(win32_resource_lock(icon_resource),
        win32_resource_size(module, icon_resource), requested_width,
        requested_height);
}

bool win32_icon_get(void *handle, const uint32_t **pixels, int *width,
                    int *height) {
    icon_handle_t *icon = icon_from_handle(handle);
    if (!icon) return false;
    if (pixels) *pixels = icon->pixels;
    if (width) *width = icon->width;
    if (height) *height = icon->height;
    return true;
}

void *win32_icon_copy(void *handle) {
    icon_handle_t *source = icon_from_handle(handle);
    void *copy;
    icon_handle_t *destination;
    if (!source) return NULL;
    copy = icon_allocate(source->width, source->height, false);
    destination = icon_from_handle(copy);
    if (!destination) return NULL;
    kmemcpy(destination->pixels, source->pixels,
            (size_t)source->width * (size_t)source->height * sizeof(uint32_t));
    return copy;
}

bool win32_icon_destroy(void *handle) {
    icon_handle_t *icon = icon_from_handle(handle);
    if (!icon) return false;
    if (icon->shared) return true;
    kfree(icon->pixels);
    kmemset(icon, 0, sizeof(*icon));
    return true;
}


void win32_resource_cleanup_process(uint32_t pid) {
    uint32_t released_resources = 0U;
    uint32_t released_icons = 0U;

    task_preempt_disable();
    for (uint32_t i = 0; i < RESOURCE_HANDLE_MAX; i++) {
        if (resource_handles[i].used && resource_handles[i].pid == pid) {
            kmemset(&resource_handles[i], 0, sizeof(resource_handles[i]));
            released_resources++;
        }
    }
    for (uint32_t i = 0; i < ICON_HANDLE_MAX; i++) {
        if (!icon_handles[i].used || icon_handles[i].pid != pid) continue;
        kfree(icon_handles[i].pixels);
        kmemset(&icon_handles[i], 0, sizeof(icon_handles[i]));
        released_icons++;
    }
    task_preempt_enable();

    if (released_resources || released_icons) {
        kprintf("[RES:CLEANUP] pid=%u resources=%u icons=%u\n",
                pid, released_resources, released_icons);
    }
}
