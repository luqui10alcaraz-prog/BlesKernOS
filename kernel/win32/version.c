#include "win32.h"
#include "../include/memory.h"
#include "../include/vfs.h"
#include "../stdio.h"

/* BLES_WINE_REAL_VERSIONINFO_20260723 */

#define VERSION_MAGIC 0x56455242U
#define PE_SIGNATURE 0x00004550U
#define RT_VERSION_ID 16U

typedef struct PACKED {
    uint32_t signature, struct_version, file_version_ms, file_version_ls;
    uint32_t product_version_ms, product_version_ls, flags_mask, flags;
    uint32_t os, type, subtype, date_ms, date_ls;
} fixed_version_t;

typedef struct PACKED {
    uint32_t magic;
    fixed_version_t fixed;
    uint32_t translation;
    uint16_t empty[2];
} fallback_version_blob_t;

typedef struct PACKED {
    uint16_t magic;
    uint8_t unused[58];
    uint32_t pe_offset;
} dos_header_t;

typedef struct PACKED {
    uint16_t machine;
    uint16_t section_count;
    uint32_t timestamp;
    uint32_t symbols;
    uint32_t symbol_count;
    uint16_t optional_size;
    uint16_t characteristics;
} coff_header_t;

typedef struct PACKED {
    uint16_t magic;
    uint8_t linker_major, linker_minor;
    uint32_t code_size, initialized_size, uninitialized_size;
    uint32_t entry_rva, code_base, data_base;
    uint32_t image_base, section_alignment, file_alignment;
    uint16_t os_major, os_minor, image_major, image_minor;
    uint16_t subsystem_major, subsystem_minor;
    uint32_t win32_version, image_size, headers_size, checksum;
    uint16_t subsystem, dll_characteristics;
    uint32_t stack_reserve, stack_commit, heap_reserve, heap_commit;
    uint32_t loader_flags, directory_count;
} optional32_prefix_t;

typedef struct PACKED {
    uint32_t virtual_address;
    uint32_t size;
} data_directory_t;

typedef struct PACKED {
    uint8_t name[8];
    uint32_t virtual_size;
    uint32_t virtual_address;
    uint32_t raw_size;
    uint32_t raw_offset;
    uint32_t relocations;
    uint32_t line_numbers;
    uint16_t relocation_count;
    uint16_t line_count;
    uint32_t characteristics;
} section_header_t;

typedef struct PACKED {
    uint32_t characteristics;
    uint32_t timestamp;
    uint16_t major_version;
    uint16_t minor_version;
    uint16_t named_entries;
    uint16_t id_entries;
} resource_directory_t;

typedef struct PACKED {
    uint32_t name;
    uint32_t offset;
} resource_entry_t;

typedef struct PACKED {
    uint32_t data_rva;
    uint32_t size;
    uint32_t code_page;
    uint32_t reserved;
} resource_data_entry_t;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static bool wide_equal_ascii(const uint16_t *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b && *a == (uint8_t)*b) {
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static bool version_native_path(const char *input, char *output,
                                uint32_t capacity) {
    uint32_t source = 0U, destination = 0U;

    if (!input || !input[0] || !output || capacity < 2U) return false;

    if (((input[0] >= 'A' && input[0] <= 'Z') ||
         (input[0] >= 'a' && input[0] <= 'z')) &&
        input[1] == ':')
        source = 2U;

    if (input[source] != '/' && input[source] != '\\')
        output[destination++] = '/';

    while (input[source] && destination + 1U < capacity) {
        char value = input[source++];
        if (value == '\\') value = '/';
        if (value == '/' && destination > 0U &&
            output[destination - 1U] == '/')
            continue;
        output[destination++] = value;
    }

    output[destination] = 0;
    return input[source] == 0 && destination > 0U;
}

static bool version_wide_to_ansi(const uint16_t *input, char *output,
                                 uint32_t capacity) {
    uint32_t index = 0U;
    if (!input || !output || capacity == 0U) return false;
    while (input[index] && index + 1U < capacity) {
        output[index] = input[index] <= 0xFFU ? (char)input[index] : '?';
        index++;
    }
    output[index] = 0;
    return input[index] == 0;
}

static bool range_ok(uint32_t offset, uint32_t bytes, uint32_t size) {
    return offset <= size && bytes <= size - offset;
}

static bool rva_to_offset(uint32_t rva,
                          const section_header_t *sections,
                          uint16_t section_count,
                          uint32_t headers_size,
                          uint32_t file_size,
                          uint32_t *offset) {
    if (!offset) return false;

    if (rva < headers_size && rva < file_size) {
        *offset = rva;
        return true;
    }

    for (uint16_t i = 0U; i < section_count; i++) {
        uint32_t span = sections[i].virtual_size;
        uint32_t relative;

        if (span < sections[i].raw_size) span = sections[i].raw_size;
        if (rva < sections[i].virtual_address ||
            rva >= sections[i].virtual_address + span)
            continue;

        relative = rva - sections[i].virtual_address;
        if (relative >= sections[i].raw_size) return false;
        if (!range_ok(sections[i].raw_offset, relative + 1U, file_size))
            return false;

        *offset = sections[i].raw_offset + relative;
        return true;
    }

    return false;
}

static bool resource_directory_entry(const uint8_t *resource_base,
                                     uint32_t resource_size,
                                     uint32_t directory_offset,
                                     uint32_t wanted_id,
                                     bool select_first,
                                     resource_entry_t *result) {
    const resource_directory_t *directory;
    const resource_entry_t *entries;
    uint32_t count;

    if (!result ||
        !range_ok(directory_offset, sizeof(resource_directory_t),
                  resource_size))
        return false;

    directory = (const resource_directory_t *)(resource_base +
                                               directory_offset);
    count = (uint32_t)directory->named_entries +
            (uint32_t)directory->id_entries;

    if (!range_ok(directory_offset + sizeof(*directory),
                  count * sizeof(resource_entry_t), resource_size))
        return false;

    entries = (const resource_entry_t *)(resource_base + directory_offset +
                                         sizeof(*directory));

    for (uint32_t i = 0U; i < count; i++) {
        bool named = (entries[i].name & 0x80000000U) != 0U;
        uint32_t id = entries[i].name & 0xFFFFU;

        if (select_first || (!named && id == wanted_id)) {
            *result = entries[i];
            return true;
        }
    }

    return false;
}

static bool version_resource_from_pe(const uint8_t *file, uint32_t file_size,
                                     const uint8_t **resource,
                                     uint32_t *resource_size) {
    const dos_header_t *dos;
    const coff_header_t *coff;
    const optional32_prefix_t *optional;
    const data_directory_t *directories;
    const section_header_t *sections;
    resource_entry_t type_entry, name_entry, language_entry;
    const resource_data_entry_t *data_entry;
    uint32_t optional_offset, section_offset;
    uint32_t resource_rva, resource_bytes, resource_file_offset;
    uint32_t data_entry_offset, data_file_offset;
    const uint8_t *resource_base;

    if (resource) *resource = NULL;
    if (resource_size) *resource_size = 0U;
    if (!file || !resource || !resource_size ||
        file_size < sizeof(dos_header_t))
        return false;

    dos = (const dos_header_t *)file;
    if (dos->magic != 0x5A4DU ||
        !range_ok(dos->pe_offset, 4U + sizeof(coff_header_t), file_size))
        return false;

    if (*(const uint32_t *)(file + dos->pe_offset) != PE_SIGNATURE)
        return false;

    coff = (const coff_header_t *)(file + dos->pe_offset + 4U);
    optional_offset = dos->pe_offset + 4U + sizeof(*coff);

    if (!range_ok(optional_offset, coff->optional_size, file_size) ||
        coff->optional_size < sizeof(optional32_prefix_t) +
                              3U * sizeof(data_directory_t))
        return false;

    optional = (const optional32_prefix_t *)(file + optional_offset);
    if (optional->magic != 0x010BU || optional->directory_count <= 2U)
        return false;

    directories = (const data_directory_t *)(file + optional_offset +
                                             sizeof(*optional));
    resource_rva = directories[2].virtual_address;
    resource_bytes = directories[2].size;
    if (!resource_rva || !resource_bytes) return false;

    section_offset = optional_offset + coff->optional_size;
    if (!range_ok(section_offset,
                  (uint32_t)coff->section_count * sizeof(section_header_t),
                  file_size))
        return false;

    sections = (const section_header_t *)(file + section_offset);
    if (!rva_to_offset(resource_rva, sections, coff->section_count,
                       optional->headers_size, file_size,
                       &resource_file_offset) ||
        !range_ok(resource_file_offset, resource_bytes, file_size))
        return false;

    resource_base = file + resource_file_offset;

    if (!resource_directory_entry(resource_base, resource_bytes, 0U,
                                  RT_VERSION_ID, false, &type_entry) ||
        !(type_entry.offset & 0x80000000U))
        return false;

    if (!resource_directory_entry(
            resource_base, resource_bytes,
            type_entry.offset & 0x7FFFFFFFU, 0U, true, &name_entry) ||
        !(name_entry.offset & 0x80000000U))
        return false;

    if (!resource_directory_entry(
            resource_base, resource_bytes,
            name_entry.offset & 0x7FFFFFFFU, 0U, true, &language_entry) ||
        (language_entry.offset & 0x80000000U))
        return false;

    data_entry_offset = language_entry.offset & 0x7FFFFFFFU;
    if (!range_ok(data_entry_offset, sizeof(resource_data_entry_t),
                  resource_bytes))
        return false;

    data_entry = (const resource_data_entry_t *)(resource_base +
                                                 data_entry_offset);

    if (!rva_to_offset(data_entry->data_rva, sections, coff->section_count,
                       optional->headers_size, file_size,
                       &data_file_offset) ||
        !range_ok(data_file_offset, data_entry->size, file_size))
        return false;

    *resource = file + data_file_offset;
    *resource_size = data_entry->size;
    return true;
}

static bool load_version_resource_a(const char *file_name,
                                    void **file_data,
                                    uint32_t *file_size,
                                    const uint8_t **resource,
                                    uint32_t *resource_size) {
    char path[VFS_MAX_PATH];

    if (file_data) *file_data = NULL;
    if (file_size) *file_size = 0U;

    if (!file_name || !file_data || !file_size || !resource ||
        !resource_size ||
        !version_native_path(file_name, path, sizeof(path)) ||
        !vfs_read_all(path, file_data, file_size))
        return false;

    if (!version_resource_from_pe((const uint8_t *)*file_data, *file_size,
                                  resource, resource_size)) {
        kfree(*file_data);
        *file_data = NULL;
        *file_size = 0U;
        return false;
    }

    return true;
}

/* BLES_WINE_BUILTIN_VERSION_PROFILES_20260723 */
static char version_ascii_lower(char value) {
    if (value >= 'A' && value <= 'Z')
        return (char)(value + ('a' - 'A'));
    return value;
}

static const char *version_basename(const char *path) {
    const char *base = path;

    if (!path) return "";

    for (const char *cursor = path; *cursor; cursor++) {
        if (*cursor == '/' || *cursor == '\\' || *cursor == ':')
            base = cursor + 1;
    }

    return base;
}

static bool version_name_equal_ci(const char *path, const char *wanted) {
    const char *name = version_basename(path);

    if (!name || !wanted) return false;

    while (*name && *wanted) {
        if (version_ascii_lower(*name) != version_ascii_lower(*wanted))
            return false;
        name++;
        wanted++;
    }

    return *name == 0 && *wanted == 0;
}

static void version_set_number(fixed_version_t *fixed,
                               uint16_t major, uint16_t minor,
                               uint16_t build, uint16_t revision) {
    fixed->file_version_ms =
        ((uint32_t)major << 16) | (uint32_t)minor;
    fixed->file_version_ls =
        ((uint32_t)build << 16) | (uint32_t)revision;
    fixed->product_version_ms = fixed->file_version_ms;
    fixed->product_version_ls = fixed->file_version_ls;
}

static void fill_fallback(fallback_version_blob_t *blob,
                          const char *file_name) {
    kmemset(blob, 0, sizeof(*blob));
    blob->magic = VERSION_MAGIC;
    blob->fixed.signature = 0xFEEF04BDU;
    blob->fixed.struct_version = 0x00010000U;
    blob->fixed.flags_mask = 0x3FU;
    blob->fixed.os = 0x00040004U;
    blob->fixed.type = 2U;
    blob->translation = 0x04E40409U;

    /* BLES_WINE_COMCTL32_REAL_BUILD_20260723
     *
     * Algunas aplicaciones Win9x comparan también dwFileVersionLS.
     * 4.72.0.0 conserva el major/minor correcto, pero parece una build
     * incompleta. Use el número histórico completo de COMCTL32 4.72.
     */
    if (version_name_equal_ci(file_name, "comctl32.dll")) {
        version_set_number(&blob->fixed, 4U, 72U, 2106U, 4U);
    } else if (version_name_equal_ci(file_name, "shell32.dll")) {
        version_set_number(&blob->fixed, 4U, 72U, 0U, 0U);
    } else if (version_name_equal_ci(file_name, "kernel32.dll") ||
               version_name_equal_ci(file_name, "user32.dll") ||
               version_name_equal_ci(file_name, "gdi32.dll") ||
               version_name_equal_ci(file_name, "advapi32.dll")) {
        version_set_number(&blob->fixed, 4U, 10U, 0U, 0U);
    } else if (version_name_equal_ci(file_name, "ole32.dll") ||
               version_name_equal_ci(file_name, "comdlg32.dll") ||
               version_name_equal_ci(file_name, "version.dll")) {
        version_set_number(&blob->fixed, 4U, 71U, 0U, 0U);
    } else {
        version_set_number(&blob->fixed, 4U, 0U, 0U, 0U);
    }

    kprintf("[VERSION] builtin profile %s -> %u.%u.%u.%u\n",
            file_name ? file_name : "(null)",
            blob->fixed.file_version_ms >> 16,
            blob->fixed.file_version_ms & 0xFFFFU,
            blob->fixed.file_version_ls >> 16,
            blob->fixed.file_version_ls & 0xFFFFU);
}

/* BLES_WINE_VERSIONINFO_WINE_ANSI_20260723
 *
 * Wine/Windows reserve roughly twice the RT_VERSION size plus four bytes.
 * VerQueryValueA uses that trailing region to materialize ANSI copies of
 * UTF-16 text values while leaving the original version resource untouched.
 */
static uint32_t version_info_buffer_size(uint32_t resource_size) {
    if (resource_size > (0xFFFFFFFFU - 4U) / 2U)
        return 0U;
    return resource_size * 2U + 4U;
}

static uint32_t version_declared_length(const void *block) {
    if (!block) return 0U;
    return (uint32_t)*(const uint16_t *)block;
}

static void version_write_conversion_signature(void *block,
                                               uint32_t capacity) {
    static const uint8_t signature[4] = {'F', 'E', '2', 'X'};
    uint32_t declared = version_declared_length(block);

    if (!block || declared > capacity || capacity - declared < 4U)
        return;

    kmemcpy((uint8_t *)block + declared, signature, sizeof(signature));
}

static uint32_t WIN32_API ver_GetFileVersionInfoSizeA(
    const char *file_name, uint32_t *handle) {
    void *file_data = NULL;
    uint32_t file_size = 0U;
    const uint8_t *resource = NULL;
    uint32_t resource_size = 0U;
    uint32_t buffer_size;

    if (handle) *handle = 0U;

    if (load_version_resource_a(file_name, &file_data, &file_size,
                                &resource, &resource_size)) {
        buffer_size = version_info_buffer_size(resource_size);
        kprintf("[VERSION] real %s resource=%u buffer=%u\n",
                file_name ? file_name : "(null)",
                resource_size, buffer_size);
        kfree(file_data);
        return buffer_size;
    }

    buffer_size = version_info_buffer_size(
        (uint32_t)sizeof(fallback_version_blob_t));
    kprintf("[VERSION] fallback %s buffer=%u\n",
            file_name ? file_name : "(null)", buffer_size);
    return buffer_size;
}

static uint32_t WIN32_API ver_GetFileVersionInfoSizeW(
    const uint16_t *file_name, uint32_t *handle) {
    char ansi[VFS_MAX_PATH];
    if (!version_wide_to_ansi(file_name, ansi, sizeof(ansi))) {
        if (handle) *handle = 0U;
        return 0U;
    }
    return ver_GetFileVersionInfoSizeA(ansi, handle);
}

static int WIN32_API ver_GetFileVersionInfoA(
    const char *file_name, uint32_t handle UNUSED,
    uint32_t length, void *data) {
    void *file_data = NULL;
    uint32_t file_size = 0U;
    const uint8_t *resource = NULL;
    uint32_t resource_size = 0U;
    uint32_t required;

    if (!data) return 0;

    if (load_version_resource_a(file_name, &file_data, &file_size,
                                &resource, &resource_size)) {
        required = version_info_buffer_size(resource_size);
        if (!required || length < required) {
            kprintf("[VERSION:INFO] FAIL file=%s buffer=%u required=%u\n",
                    file_name ? file_name : "(null)",
                    length, required);
            kfree(file_data);
            return 0;
        }

        kmemset(data, 0, required);
        kmemcpy(data, resource, resource_size);
        version_write_conversion_signature(data, required);

        kprintf("[VERSION:INFO] OK file=%s buffer=%u resource=%u data=%x\n",
                file_name ? file_name : "(null)",
                length, resource_size,
                (uint32_t)(uintptr_t)data);
        kfree(file_data);
        return 1;
    }

    required = version_info_buffer_size(
        (uint32_t)sizeof(fallback_version_blob_t));
    if (!required || length < required) {
        kprintf("[VERSION:INFO] FALLBACK FAIL file=%s buffer=%u required=%u\n",
                file_name ? file_name : "(null)",
                length, required);
        return 0;
    }

    kmemset(data, 0, required);
    fill_fallback((fallback_version_blob_t *)data, file_name);
    return 1;
}

static int WIN32_API ver_GetFileVersionInfoW(
    const uint16_t *file_name, uint32_t handle,
    uint32_t length, void *data) {
    char ansi[VFS_MAX_PATH];
    if (!version_wide_to_ansi(file_name, ansi, sizeof(ansi))) return 0;
    return ver_GetFileVersionInfoA(ansi, handle, length, data);
}

static fixed_version_t *find_fixed_version(void *block) {
    uint8_t *bytes = (uint8_t *)block;
    uint32_t total;

    if (!block) return NULL;

    total = *(uint16_t *)bytes;
    if (total < sizeof(fixed_version_t))
        total = sizeof(fallback_version_blob_t);
    if (total > 65535U) total = 65535U;

    for (uint32_t offset = 0U;
         offset + sizeof(fixed_version_t) <= total;
         offset += 4U) {
        fixed_version_t *fixed = (fixed_version_t *)(bytes + offset);
        if (fixed->signature == 0xFEEF04BDU)
            return fixed;
    }

    {
        fallback_version_blob_t *fallback =
            (fallback_version_blob_t *)block;
        if (fallback->magic == VERSION_MAGIC &&
            fallback->fixed.signature == 0xFEEF04BDU)
            return &fallback->fixed;
    }

    return NULL;
}

static int query_value(void *block, bool root, bool translation,
                       void **value, uint32_t *length) {
    fixed_version_t *fixed;

    if (value) *value = NULL;
    if (length) *length = 0U;
    if (!block || !value || !length) return 0;

    if (root) {
        fixed = find_fixed_version(block);
        if (!fixed) return 0;
        *value = fixed;
        *length = sizeof(*fixed);
        kprintf("[VERSION] root file=%u.%u.%u.%u product=%u.%u.%u.%u\n",
                fixed->file_version_ms >> 16,
                fixed->file_version_ms & 0xFFFFU,
                fixed->file_version_ls >> 16,
                fixed->file_version_ls & 0xFFFFU,
                fixed->product_version_ms >> 16,
                fixed->product_version_ms & 0xFFFFU,
                fixed->product_version_ls >> 16,
                fixed->product_version_ls & 0xFFFFU);
        return 1;
    }

    if (translation) {
        fallback_version_blob_t *fallback =
            (fallback_version_blob_t *)block;
        static uint32_t default_translation = 0x04E40409U;

        if (fallback->magic == VERSION_MAGIC) {
            *value = &fallback->translation;
            *length = 4U;
            return 1;
        }

        *value = &default_translation;
        *length = 4U;
        return 1;
    }

    return 0;
}

/* BLES_WINE_VERSION_QUERY_DIAGNOSTICS_V2_20260723 */
/* BLES_WINE_VERSION_STRINGFILEINFO_20260723 */
typedef struct PACKED {
    uint16_t length;
    uint16_t value_length;
    uint16_t type;
} version_block_header_t;

static uint32_t version_align4(uint32_t value) {
    return (value + 3U) & ~3U;
}

static bool version_block_layout(
    uint8_t *base, uint32_t available,
    version_block_header_t **header,
    uint16_t **key,
    uint8_t **value,
    uint32_t *value_bytes,
    uint8_t **children) {
    version_block_header_t *h;
    uint32_t key_offset = sizeof(version_block_header_t);
    uint32_t cursor;
    uint32_t bytes;

    if (!base || available < sizeof(version_block_header_t))
        return false;

    h = (version_block_header_t *)base;
    if (h->length < sizeof(version_block_header_t) ||
        h->length > available)
        return false;

    while (key_offset + 2U <= h->length) {
        uint16_t character = *(uint16_t *)(base + key_offset);
        key_offset += 2U;
        if (!character) break;
    }

    if (key_offset > h->length ||
        *(uint16_t *)(base + key_offset - 2U) != 0U)
        return false;

    cursor = version_align4(key_offset);
    if (cursor > h->length) return false;

    bytes = h->type == 1U
        ? (uint32_t)h->value_length * 2U
        : (uint32_t)h->value_length;

    if (bytes > h->length - cursor) return false;

    if (header) *header = h;
    if (key) *key = (uint16_t *)(base + sizeof(version_block_header_t));
    if (value) *value = base + cursor;
    if (value_bytes) *value_bytes = bytes;

    cursor = version_align4(cursor + bytes);
    if (cursor > h->length) cursor = h->length;
    if (children) *children = base + cursor;
    return true;
}

static bool version_key_equal_component(const uint16_t *wide,
                                        const char *component,
                                        uint32_t length) {
    uint32_t index = 0U;

    if (!wide || !component) return false;

    while (index < length && wide[index]) {
        char a = wide[index] <= 0xFFU ? (char)wide[index] : '?';
        char b = component[index];

        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));

        if (a != b) return false;
        index++;
    }

    return index == length && wide[index] == 0U;
}

static uint8_t *version_find_child(uint8_t *parent,
                                   const char *component,
                                   uint32_t component_length) {
    version_block_header_t *parent_header;
    uint8_t *children;
    uint8_t *parent_end;
    uint8_t *cursor;

    if (!version_block_layout(
            parent, 65535U, &parent_header, NULL,
            NULL, NULL, &children))
        return NULL;

    parent_end = parent + parent_header->length;
    cursor = children;

    while (cursor + sizeof(version_block_header_t) <= parent_end) {
        version_block_header_t *child_header;
        uint16_t *child_key;
        uint32_t remaining = (uint32_t)(parent_end - cursor);

        if (!version_block_layout(
                cursor, remaining, &child_header, &child_key,
                NULL, NULL, NULL))
            break;

        if (version_key_equal_component(
                child_key, component, component_length))
            return cursor;

        if (!child_header->length) break;
        cursor += version_align4(child_header->length);
    }

    return NULL;
}

static int version_query_path_a(void *block, const char *subblock,
                                void **value, uint32_t *length,
                                bool *is_text) {
    uint8_t *current = (uint8_t *)block;
    const char *cursor = subblock;
    version_block_header_t *header;
    uint8_t *block_value;
    uint32_t block_value_bytes;

    if (is_text) *is_text = false;
    if (!block || !subblock || !value || !length) return 0;

    /*
     * Builtin modules without an on-disk PE receive our compact fallback
     * version blob.  It only contains VS_FIXEDFILEINFO and Translation;
     * unlike a VS_VERSIONINFO resource it has no child blocks.  Letting the
     * generic resource walker parse it treats VERSION_MAGIC as a length and
     * walks past the allocation into unrelated process memory.  WinZip then
     * found a spurious "3,999" value for WZINET32.DLL and rejected the
     * optional browser-support shim as an incompatible add-on.
     */
    if (((fallback_version_blob_t *)block)->magic == VERSION_MAGIC)
        return 0;

    while (*cursor == '\\') cursor++;
    if (!*cursor) return 0;

    while (*cursor) {
        const char *component = cursor;
        uint32_t component_length = 0U;

        while (cursor[component_length] &&
               cursor[component_length] != '\\')
            component_length++;

        current = version_find_child(
            current, component, component_length);
        if (!current) return 0;

        cursor += component_length;
        while (*cursor == '\\') cursor++;
    }

    if (!version_block_layout(
            current, 65535U, &header, NULL,
            &block_value, &block_value_bytes, NULL))
        return 0;

    *value = block_value;
    *length = header->type == 1U
        ? (uint32_t)header->value_length
        : block_value_bytes;
    if (is_text) *is_text = header->type == 1U;

    return 1;
}

static int version_query_path_w(void *block,
                                const uint16_t *subblock,
                                void **value, uint32_t *length,
                                bool *is_text) {
    char ansi[192];
    uint32_t index = 0U;

    if (is_text) *is_text = false;
    if (!subblock) return 0;

    while (subblock[index] && index + 1U < sizeof(ansi)) {
        ansi[index] =
            subblock[index] <= 0xFFU ? (char)subblock[index] : '?';
        index++;
    }
    ansi[index] = 0;

    return version_query_path_a(
        block, ansi, value, length, is_text);
}

/* BLES_WINE_VERSION_ROOT_QUERY_FIX_20260723
 *
 * Avoid escaped-string ambiguity: the root query is exactly one backslash
 * followed by NUL, in ANSI or UTF-16 form.
 */
static bool version_is_root_a(const char *subblock) {
    return subblock && subblock[0] == '\\' && subblock[1] == 0;
}

static bool version_is_root_w(const uint16_t *subblock) {
    return subblock && subblock[0] == (uint16_t)'\\' && subblock[1] == 0U;
}

static bool version_path_equal_ci(const char *left,
                                  const char *right) {
    if (!left || !right) return false;

    while (*left == '\\') left++;
    while (*right == '\\') right++;

    while (*left && *right) {
        char a = version_ascii_lower(*left);
        char b = version_ascii_lower(*right);

        if (a != b) return false;
        left++;
        right++;
    }

    return *left == 0 && *right == 0;
}

static bool version_is_translation_a(const char *subblock) {
    return version_path_equal_ci(
        subblock, "VarFileInfo\\Translation");
}

static bool version_is_translation_w(const uint16_t *subblock) {
    char ansi[64];
    uint32_t index = 0U;

    if (!subblock) return false;

    while (subblock[index] && index + 1U < sizeof(ansi)) {
        ansi[index] =
            subblock[index] <= 0xFFU ? (char)subblock[index] : '?';
        index++;
    }
    ansi[index] = 0;

    return version_is_translation_a(ansi);
}

static int version_text_value_to_ansi(
    void *block, void *wide_value, uint32_t wide_length,
    void **value, uint32_t *length) {
    uint8_t *base = (uint8_t *)block;
    uint8_t *source_bytes = (uint8_t *)wide_value;
    const uint16_t *source = (const uint16_t *)wide_value;
    uint32_t declared = version_declared_length(block);
    uint32_t source_offset;
    uint32_t capacity;
    uint32_t converted;
    char *destination;

    if (!block || !wide_value || !value || !length ||
        source_bytes < base)
        return 0;

    source_offset = (uint32_t)(source_bytes - base);
    if (!declared || source_offset > declared)
        return 0;

    capacity = declared - source_offset;
    converted = wide_length;
    if (converted > capacity)
        converted = capacity;

    destination = (char *)base + declared + 4U + source_offset;

    for (uint32_t index = 0U; index < converted; index++) {
        uint16_t character = source[index];
        destination[index] =
            character <= 0x00FFU ? (char)character : '?';
    }

    if (!converted || destination[converted - 1U] != 0) {
        if (converted >= capacity)
            return 0;
        destination[converted++] = 0;
    }

    *value = destination;
    *length = converted;
    return 1;
}

static int WIN32_API ver_VerQueryValueA(
    void *block, const char *subblock,
    void **value, uint32_t *length) {
    static const char root_path[] = "\\";
    bool root;
    bool translation;
    bool is_text = false;
    int result;

    if (!subblock || !subblock[0])
        subblock = root_path;

    root = version_is_root_a(subblock);
    translation = version_is_translation_a(subblock);

    kprintf("[VERSION:QUERY:A] block=%x sub=%s root=%u translation=%u\n",
            (uint32_t)(uintptr_t)block,
            subblock,
            root ? 1U : 0U,
            translation ? 1U : 0U);

    result = query_value(block, root, translation, value, length);
    if (!result)
        result = version_query_path_a(
            block, subblock, value, length, &is_text);

    if (result && is_text) {
        void *wide_value = *value;
        uint32_t wide_length = *length;

        result = version_text_value_to_ansi(
            block, wide_value, wide_length, value, length);

        if (result) {
            kprintf("[VERSION:ANSI] sub=%s value=%s length=%u\n",
                    subblock, (const char *)*value, *length);
        }
    }

    kprintf("[VERSION:QUERY:A] result=%d value=%x length=%u text=%u\n",
            result,
            value && *value ? (uint32_t)(uintptr_t)*value : 0U,
            length ? *length : 0U,
            is_text ? 1U : 0U);
    return result;
}

static int WIN32_API ver_VerQueryValueW(
    void *block, const uint16_t *subblock,
    void **value, uint32_t *length) {
    static const uint16_t root_path[] = {'\\', 0U};
    char printable[160];
    uint32_t index = 0U;
    bool root;
    bool translation;
    bool is_text = false;
    int result;

    if (!subblock || !subblock[0])
        subblock = root_path;

    root = version_is_root_w(subblock);
    translation = version_is_translation_w(subblock);

    while (subblock[index] && index + 1U < sizeof(printable)) {
        printable[index] =
            subblock[index] <= 0xFFU ? (char)subblock[index] : '?';
        index++;
    }
    printable[index] = 0;

    kprintf("[VERSION:QUERY:W] block=%x sub=%s root=%u translation=%u\n",
            (uint32_t)(uintptr_t)block,
            printable,
            root ? 1U : 0U,
            translation ? 1U : 0U);

    result = query_value(block, root, translation, value, length);
    if (!result)
        result = version_query_path_w(
            block, subblock, value, length, &is_text);

    kprintf("[VERSION:QUERY:W] result=%d value=%x length=%u text=%u\n",
            result,
            value && *value ? (uint32_t)(uintptr_t)*value : 0U,
            length ? *length : 0U,
            is_text ? 1U : 0U);
    return result;
}

uint32_t win32_version_resolve(const char *name) {
#define V(api) if (equal(name, #api)) return (uint32_t)(uintptr_t)&ver_##api
    V(GetFileVersionInfoSizeA);
    V(GetFileVersionInfoSizeW);
    V(GetFileVersionInfoA);
    V(GetFileVersionInfoW);
    V(VerQueryValueA);
    V(VerQueryValueW);
#undef V
    return 0;
}
