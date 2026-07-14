#include "resources.h"
#include "../include/pe_loader.h"
#include "../include/task.h"
#include "../include/memory.h"

#define RESOURCE_HANDLE_BASE 0x77000000U
#define RESOURCE_HANDLE_MAX  48U
#define RESOURCE_DIRECTORY_FLAG 0x80000000U
#define RESOURCE_NAME_FLAG      0x80000000U

#define ERROR_RESOURCE_DATA_NOT_FOUND 1812U
#define ERROR_RESOURCE_TYPE_NOT_FOUND 1813U
#define ERROR_RESOURCE_NAME_NOT_FOUND 1814U
#define ERROR_RESOURCE_LANG_NOT_FOUND 1815U
#define ERROR_INVALID_HANDLE 6U

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
    task_preempt_disable();
    for (uint32_t i = 0; i < RESOURCE_HANDLE_MAX; i++) {
        if (resource_handles[i].used) continue;
        resource_handles[i].used = true;
        resource_handles[i].pid = task_current_process_id();
        resource_handles[i].module = module;
        resource_handles[i].data = data;
        resource_handles[i].size = size;
        resource_handles[i].language = language;
        task_preempt_enable();
        return (void *)(uintptr_t)(RESOURCE_HANDLE_BASE + i);
    }
    task_preempt_enable();
    return NULL;
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


void win32_resource_cleanup_process(uint32_t pid) {
    task_preempt_disable();
    for (uint32_t i = 0; i < RESOURCE_HANDLE_MAX; i++) {
        if (resource_handles[i].used && resource_handles[i].pid == pid)
            resource_handles[i].used = false;
    }
    task_preempt_enable();
}
