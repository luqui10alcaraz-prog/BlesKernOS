#include "include/pe_loader.h"
#include "include/elf_loader.h"
#include "include/memory.h"
#include "include/task.h"
#include "include/vfs.h"
#include "include/pit.h"
#include "win32/win32.h"
#include "win32/process.h"
#include "win32/resources.h"
#include "stdio.h"

#define PE_DOS_MAGIC                 0x5A4DU
#define PE_NT_SIGNATURE              0x00004550U
#define PE_MACHINE_I386              0x014CU
#define PE_OPTIONAL_MAGIC_PE32       0x010BU
#define PE_MAX_SECTIONS              96U
#define PE_MAX_IMAGE_SIZE            (32U * 1024U * 1024U)
#define PE_DIRECTORY_IMPORT          1U
#define PE_DIRECTORY_EXPORT          0U
#define PE_DIRECTORY_RESOURCE        2U
#define PE_DIRECTORY_BASERELOC       5U
#define PE_DIRECTORY_TLS             9U
#define PE_DIRECTORY_COUNT           16U
#define PE_ORDINAL_FLAG32            0x80000000U
#define PE_RELOC_ABSOLUTE            0U
#define PE_RELOC_HIGHLOW             3U
#define PE_STD_INPUT_HANDLE          ((int32_t)-10)
#define PE_STD_OUTPUT_HANDLE         ((int32_t)-11)
#define PE_STD_ERROR_HANDLE          ((int32_t)-12)
#define PE_ERROR_INVALID_HANDLE      6U
#define PE_ERROR_INVALID_PARAMETER   87U
#define PE_ERROR_CALL_NOT_IMPLEMENTED 120U

#define WINAPI __attribute__((stdcall))

typedef struct {
    uint32_t virtual_address;
    uint32_t size;
} PACKED pe_data_directory_t;

typedef struct {
    uint16_t machine;
    uint16_t number_of_sections;
    uint32_t timestamp;
    uint32_t symbol_table;
    uint32_t number_of_symbols;
    uint16_t size_of_optional_header;
    uint16_t characteristics;
} PACKED pe_file_header_t;

typedef struct {
    uint16_t magic;
    uint8_t major_linker_version;
    uint8_t minor_linker_version;
    uint32_t size_of_code;
    uint32_t size_of_initialized_data;
    uint32_t size_of_uninitialized_data;
    uint32_t address_of_entry_point;
    uint32_t base_of_code;
    uint32_t base_of_data;
    uint32_t image_base;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint16_t major_os_version;
    uint16_t minor_os_version;
    uint16_t major_image_version;
    uint16_t minor_image_version;
    uint16_t major_subsystem_version;
    uint16_t minor_subsystem_version;
    uint32_t win32_version_value;
    uint32_t size_of_image;
    uint32_t size_of_headers;
    uint32_t checksum;
    uint16_t subsystem;
    uint16_t dll_characteristics;
    uint32_t size_of_stack_reserve;
    uint32_t size_of_stack_commit;
    uint32_t size_of_heap_reserve;
    uint32_t size_of_heap_commit;
    uint32_t loader_flags;
    uint32_t number_of_rva_and_sizes;
    pe_data_directory_t data_directory[PE_DIRECTORY_COUNT];
} PACKED pe_optional_header32_t;

typedef struct {
    uint32_t signature;
    pe_file_header_t file_header;
    pe_optional_header32_t optional_header;
} PACKED pe_nt_headers32_t;

typedef struct {
    uint8_t name[8];
    uint32_t virtual_size;
    uint32_t virtual_address;
    uint32_t size_of_raw_data;
    uint32_t pointer_to_raw_data;
    uint32_t pointer_to_relocations;
    uint32_t pointer_to_line_numbers;
    uint16_t number_of_relocations;
    uint16_t number_of_line_numbers;
    uint32_t characteristics;
} PACKED pe_section_header_t;

typedef struct {
    uint32_t original_first_thunk;
    uint32_t timestamp;
    uint32_t forwarder_chain;
    uint32_t name;
    uint32_t first_thunk;
} PACKED pe_import_descriptor_t;

typedef struct {
    uint32_t virtual_address;
    uint32_t size_of_block;
} PACKED pe_base_relocation_t;

typedef struct {
    uint32_t characteristics, timestamp;
    uint16_t major_version, minor_version;
    uint32_t name, ordinal_base, number_of_functions, number_of_names;
    uint32_t address_of_functions, address_of_names, address_of_name_ordinals;
} PACKED pe_export_directory_t;
typedef struct {
    uint32_t start_raw, end_raw, address_of_index, address_of_callbacks;
    uint32_t zero_fill, characteristics;
} PACKED pe_tls_directory32_t;

typedef struct {
    uint8_t *base;
    uint32_t size;
    uint32_t preferred_base;
    uint32_t entry_rva;
    bool is_dll;
    uint32_t references;
    char module_name[32];
    char path[VFS_MAX_PATH];
} pe_loaded_image_t;

typedef struct {
    uint32_t pid;
    pe_loaded_image_t *image;
} pe_process_slot_t;

static const char *g_pe_error = "sin error";
static uint32_t g_win32_last_error; /* fallback before a TEB exists */
static pe_process_slot_t g_pe_processes[TASK_MAX];
static pe_loaded_image_t *g_pe_modules[16];

static bool pe_range_ok(uint32_t offset, uint32_t length, uint32_t total) {
    return offset <= total && length <= total - offset;
}

static uint32_t pe_align_up(uint32_t value, uint32_t alignment) {
    if (alignment <= 1U) return value;
    if (value > 0xFFFFFFFFU - (alignment - 1U)) return 0;
    return (value + alignment - 1U) & ~(alignment - 1U);
}

static uint8_t pe_ascii_upper(uint8_t c) {
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - ('a' - 'A'));
    return c;
}

static bool pe_ascii_equal_ci(const char *left, const char *right) {
    if (!left || !right) return false;
    while (*left && *right) {
        if (pe_ascii_upper((uint8_t)*left) != pe_ascii_upper((uint8_t)*right))
            return false;
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static bool pe_string_in_image(const pe_loaded_image_t *image,
                               uint32_t rva, const char **string_out) {
    const char *text;
    uint32_t remaining;

    if (!image || !string_out || rva >= image->size) return false;
    text = (const char *)(image->base + rva);
    remaining = image->size - rva;
    for (uint32_t i = 0; i < remaining; i++) {
        if (text[i] == '\0') {
            *string_out = text;
            return true;
        }
    }
    return false;
}

static void *pe_rva_ptr(pe_loaded_image_t *image, uint32_t rva,
                        uint32_t length) {
    if (!image || !pe_range_ok(rva, length, image->size)) return NULL;
    return image->base + rva;
}

static uint32_t WINAPI pe_k32_GetLastError(void) {
    return pe_win32_get_last_error();
}

uint32_t pe_win32_get_last_error(void) {
    if (win32_process_current_teb()) return win32_process_get_last_error();
    return g_win32_last_error;
}

void pe_win32_set_last_error(uint32_t error) {
    if (win32_process_current_teb()) win32_process_set_last_error(error);
    else g_win32_last_error = error;
}

static void WINAPI pe_k32_SetLastError(uint32_t error) {
    pe_win32_set_last_error(error);
}

static void *WINAPI pe_k32_GetStdHandle(int32_t handle) {
    if (handle == PE_STD_INPUT_HANDLE) return (void *)(uintptr_t)0;
    if (handle == PE_STD_OUTPUT_HANDLE) return (void *)(uintptr_t)1;
    if (handle == PE_STD_ERROR_HANDLE) return (void *)(uintptr_t)2;
    pe_win32_set_last_error(PE_ERROR_INVALID_PARAMETER);
    return (void *)(uintptr_t)0xFFFFFFFFU;
}

static int WINAPI pe_k32_WriteFile(void *handle, const void *buffer,
                                   uint32_t length, uint32_t *written,
                                   void *overlapped UNUSED) {
    uint32_t fd = (uint32_t)(uintptr_t)handle;
    const uint8_t *bytes = (const uint8_t *)buffer;

    if ((uint32_t)(uintptr_t)handle >= 0x71000000U)
        return win32_file_write(handle, buffer, length, written);
    if (written) *written = 0;
    if ((fd != 1U && fd != 2U) || (!buffer && length != 0U)) {
        pe_win32_set_last_error(PE_ERROR_INVALID_HANDLE);
        return 0;
    }
    for (uint32_t i = 0; i < length; i++) putchar(bytes[i]);
    if (written) *written = length;
    pe_win32_set_last_error(0);
    return 1;
}

static uint32_t WINAPI pe_k32_GetCurrentProcessId(void) {
    return task_current_process_id();
}

static uint32_t WINAPI pe_k32_GetCurrentThreadId(void) {
    return task_current_pid();
}

static uint32_t WINAPI pe_k32_GetTickCount(void) {
    uint32_t hz = pit_get_frequency_hz();
    uint64_t milliseconds;

    if (!hz) return 0U;
    milliseconds = (uint64_t)pit_get_ticks() * 1000U;
    return (uint32_t)(milliseconds / hz);
}

static void WINAPI pe_k32_Sleep(uint32_t milliseconds) {
    uint32_t hz = pit_get_frequency_hz();
    uint64_t scaled;
    uint32_t ticks;

    if (milliseconds == 0U || !hz) {
        task_yield();
        return;
    }
    scaled = (uint64_t)milliseconds * (uint64_t)hz + 999U;
    ticks = (uint32_t)(scaled / 1000U);
    task_sleep(ticks ? ticks : 1U);
}

static void pe_call_tls_callbacks(pe_loaded_image_t *image, uint32_t reason);

static void pe_cleanup_process(uint32_t pid) {
    pe_loaded_image_t *image = NULL;

    task_preempt_disable();
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        if (g_pe_processes[i].pid != pid) continue;
        image = g_pe_processes[i].image;
        break;
    }
    task_preempt_enable();

    /* Los callbacks de detach todavia deben poder consultar el modulo actual. */
    if (image) pe_call_tls_callbacks(image, 0U); /* DLL_PROCESS_DETACH */

    task_preempt_disable();
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        if (g_pe_processes[i].pid != pid) continue;
        g_pe_processes[i].pid = 0U;
        g_pe_processes[i].image = NULL;
        break;
    }
    task_preempt_enable();
    win32_resource_cleanup_process(pid);
    win32_process_destroy(pid);
    if (image) {
        if (image->base) kfree(image->base);
        kfree(image);
    }
}

void pe_win32_terminate_current_process(void) {
    pe_cleanup_process(task_current_process_id());
    task_exit();
}

static void WINAPI pe_k32_ExitProcess(uint32_t exit_code UNUSED) {
    pe_win32_terminate_current_process();
}

static uint32_t pe_resolve_kernel32(const char *name) {
#define PE_EXPORT(symbol) \
    if (pe_ascii_equal_ci(name, #symbol)) \
        return (uint32_t)(uintptr_t)&pe_k32_##symbol

    PE_EXPORT(ExitProcess);
    PE_EXPORT(GetCurrentProcessId);
    PE_EXPORT(GetCurrentThreadId);
    PE_EXPORT(GetLastError);
    PE_EXPORT(GetStdHandle);
    PE_EXPORT(GetTickCount);
    PE_EXPORT(SetLastError);
    PE_EXPORT(Sleep);
    PE_EXPORT(WriteFile);
#undef PE_EXPORT
    return 0;
}

uint32_t pe_win32_resolve_export(const char *dll, const char *name) {
    uint32_t resolved;
    void *module;
    if (pe_ascii_equal_ci(dll, "KERNEL32.DLL") ||
        pe_ascii_equal_ci(dll, "KERNELBASE.DLL")) {
        resolved = pe_resolve_kernel32(name);
        if (resolved) return resolved;
    }
    resolved = win32_resolve_import(dll, name);
    if (resolved) return resolved;
    module = pe_win32_load_library(dll);
    return (uint32_t)(uintptr_t)pe_win32_get_proc_address(module, name);
}

uint32_t pe_win32_current_image_base(void) {
    uint32_t pid = task_current_process_id();
    uint32_t base = 0;
    task_preempt_disable();
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        if (g_pe_processes[i].pid == pid && g_pe_processes[i].image) {
            base = (uint32_t)(uintptr_t)g_pe_processes[i].image->base;
            break;
        }
    }
    task_preempt_enable();
    return base;
}

bool pe_win32_query_image_region(const void *address, const uint8_t **base_out,
                                 uint32_t *size_out) {
    uintptr_t value = (uintptr_t)address;
    pe_loaded_image_t *image = NULL;
    uint32_t pid = task_current_process_id();

    task_preempt_disable();
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        pe_loaded_image_t *candidate = g_pe_processes[i].image;
        uintptr_t start, end;
        if (g_pe_processes[i].pid != pid || !candidate || !candidate->base) continue;
        start = (uintptr_t)candidate->base;
        end = start + candidate->size;
        if (end >= start && value >= start && value < end) {
            image = candidate;
            break;
        }
    }
    if (!image) {
        for (uint32_t i = 0; i < 16U; i++) {
            pe_loaded_image_t *candidate = g_pe_modules[i];
            uintptr_t start, end;
            if (!candidate || !candidate->base) continue;
            start = (uintptr_t)candidate->base;
            end = start + candidate->size;
            if (end >= start && value >= start && value < end) {
                image = candidate;
                break;
            }
        }
    }
    if (image) {
        if (base_out) *base_out = image->base;
        if (size_out) *size_out = image->size;
    }
    task_preempt_enable();
    return image != NULL;
}

static bool pe_parse_headers(const uint8_t *file, uint32_t file_size,
                             const pe_nt_headers32_t **nt_out,
                             const pe_section_header_t **sections_out) {
    uint32_t nt_offset;
    const pe_nt_headers32_t *nt;
    uint32_t section_offset;
    uint32_t section_bytes;

    if (!file || !nt_out || !sections_out || file_size < 0x40U) {
        g_pe_error = "archivo demasiado pequeno";
        return false;
    }
    if ((uint16_t)(file[0] | ((uint16_t)file[1] << 8)) != PE_DOS_MAGIC) {
        g_pe_error = "falta firma MZ";
        return false;
    }
    nt_offset = (uint32_t)file[0x3c] |
                ((uint32_t)file[0x3d] << 8) |
                ((uint32_t)file[0x3e] << 16) |
                ((uint32_t)file[0x3f] << 24);
    if (!pe_range_ok(nt_offset, 4U + sizeof(pe_file_header_t), file_size)) {
        g_pe_error = "cabecera PE fuera del archivo";
        return false;
    }
    nt = (const pe_nt_headers32_t *)(file + nt_offset);
    if (nt->signature != PE_NT_SIGNATURE) {
        g_pe_error = "firma PE invalida";
        return false;
    }
    if (nt->file_header.machine != PE_MACHINE_I386) {
        g_pe_error = "el ejecutable no es i386";
        return false;
    }
    if ((nt->file_header.characteristics & 0x0002U) == 0U) {
        g_pe_error = "la imagen PE no es ejecutable";
        return false;
    }
    if (nt->file_header.number_of_sections == 0U ||
        nt->file_header.number_of_sections > PE_MAX_SECTIONS) {
        g_pe_error = "cantidad de secciones PE invalida";
        return false;
    }
    if (nt->file_header.size_of_optional_header <
        sizeof(pe_optional_header32_t)) {
        g_pe_error = "cabecera opcional PE32 incompleta";
        return false;
    }
    if (!pe_range_ok(nt_offset + 4U + sizeof(pe_file_header_t),
                     nt->file_header.size_of_optional_header, file_size)) {
        g_pe_error = "cabecera opcional fuera del archivo";
        return false;
    }
    if (nt->optional_header.magic != PE_OPTIONAL_MAGIC_PE32) {
        g_pe_error = "solo se soporta PE32";
        return false;
    }
    if (nt->optional_header.number_of_rva_and_sizes < PE_DIRECTORY_COUNT) {
        g_pe_error = "directorios PE incompletos";
        return false;
    }
    if (nt->optional_header.size_of_image == 0U ||
        nt->optional_header.size_of_image > PE_MAX_IMAGE_SIZE) {
        g_pe_error = "SizeOfImage invalido";
        return false;
    }
    if (nt->optional_header.address_of_entry_point >=
        nt->optional_header.size_of_image) {
        g_pe_error = "entrypoint fuera de la imagen";
        return false;
    }
    section_offset = nt_offset + 4U + sizeof(pe_file_header_t) +
                     nt->file_header.size_of_optional_header;
    section_bytes = (uint32_t)nt->file_header.number_of_sections *
                    sizeof(pe_section_header_t);
    if (!pe_range_ok(section_offset, section_bytes, file_size)) {
        g_pe_error = "tabla de secciones fuera del archivo";
        return false;
    }
    *nt_out = nt;
    *sections_out = (const pe_section_header_t *)(file + section_offset);
    return true;
}

static bool pe_map_sections(pe_loaded_image_t *image, const uint8_t *file,
                            uint32_t file_size,
                            const pe_nt_headers32_t *nt,
                            const pe_section_header_t *sections) {
    uint32_t headers_size = nt->optional_header.size_of_headers;

    if (headers_size > file_size || headers_size > image->size) {
        g_pe_error = "SizeOfHeaders invalido";
        return false;
    }
    kmemcpy(image->base, file, headers_size);

    for (uint16_t i = 0; i < nt->file_header.number_of_sections; i++) {
        const pe_section_header_t *section = &sections[i];
        uint32_t mapped_size = section->virtual_size;
        char name[9];

        if (mapped_size < section->size_of_raw_data)
            mapped_size = section->size_of_raw_data;
        if (!pe_range_ok(section->virtual_address, mapped_size, image->size)) {
            g_pe_error = "seccion fuera de SizeOfImage";
            return false;
        }
        if (section->size_of_raw_data != 0U) {
            if (!pe_range_ok(section->pointer_to_raw_data,
                             section->size_of_raw_data, file_size)) {
                g_pe_error = "datos crudos de seccion fuera del archivo";
                return false;
            }
            kmemcpy(image->base + section->virtual_address,
                    file + section->pointer_to_raw_data,
                    section->size_of_raw_data);
        }
        for (uint32_t n = 0; n < 8U; n++) name[n] = (char)section->name[n];
        name[8] = '\0';
        kprintf("[PE] seccion %s RVA=%x raw=%u virtual=%u\n",
                name, section->virtual_address, section->size_of_raw_data,
                section->virtual_size);
    }
    return true;
}

static bool pe_apply_relocations(pe_loaded_image_t *image,
                                 const pe_nt_headers32_t *nt) {
    pe_data_directory_t directory =
        nt->optional_header.data_directory[PE_DIRECTORY_BASERELOC];
    uint32_t actual_base = (uint32_t)(uintptr_t)image->base;
    uint32_t delta = actual_base - image->preferred_base;
    uint32_t consumed = 0;

    if (delta == 0U) return true;
    if (directory.virtual_address == 0U || directory.size == 0U) {
        g_pe_error = "imagen reubicada pero no tiene .reloc";
        return false;
    }
    if (!pe_range_ok(directory.virtual_address, directory.size, image->size)) {
        g_pe_error = "directorio de relocalizaciones invalido";
        return false;
    }

    while (consumed < directory.size) {
        pe_base_relocation_t *block;
        uint32_t entry_bytes;
        uint32_t entry_count;
        uint16_t *entries;

        block = (pe_base_relocation_t *)pe_rva_ptr(
            image, directory.virtual_address + consumed,
            sizeof(pe_base_relocation_t));
        if (!block || block->size_of_block < sizeof(*block) ||
            block->size_of_block > directory.size - consumed) {
            g_pe_error = "bloque .reloc malformado";
            return false;
        }
        entry_bytes = block->size_of_block - sizeof(*block);
        if ((entry_bytes & 1U) != 0U) {
            g_pe_error = "entradas .reloc desalineadas";
            return false;
        }
        entry_count = entry_bytes / sizeof(uint16_t);
        entries = (uint16_t *)(block + 1);
        for (uint32_t i = 0; i < entry_count; i++) {
            uint16_t entry = entries[i];
            uint32_t type = entry >> 12;
            uint32_t rva = block->virtual_address + (entry & 0x0FFFU);
            uint32_t *target;

            if (type == PE_RELOC_ABSOLUTE) continue;
            if (type != PE_RELOC_HIGHLOW) {
                g_pe_error = "tipo de relocalizacion PE no soportado";
                return false;
            }
            target = (uint32_t *)pe_rva_ptr(image, rva, sizeof(uint32_t));
            if (!target) {
                g_pe_error = "destino .reloc fuera de la imagen";
                return false;
            }
            *target += delta;
        }
        consumed += block->size_of_block;
    }
    return true;
}

static bool pe_fix_imports(pe_loaded_image_t *image,
                           const pe_nt_headers32_t *nt) {
    pe_data_directory_t directory =
        nt->optional_header.data_directory[PE_DIRECTORY_IMPORT];
    uint32_t offset = 0;

    if (directory.virtual_address == 0U || directory.size == 0U) return true;
    if (!pe_range_ok(directory.virtual_address, directory.size, image->size)) {
        g_pe_error = "directorio de imports invalido";
        return false;
    }

    while (offset + sizeof(pe_import_descriptor_t) <= directory.size) {
        pe_import_descriptor_t *descriptor;
        const char *dll_name;
        uint32_t lookup_rva;
        uint32_t thunk_index = 0;

        descriptor = (pe_import_descriptor_t *)pe_rva_ptr(
            image, directory.virtual_address + offset, sizeof(*descriptor));
        if (!descriptor) return false;
        if (descriptor->original_first_thunk == 0U && descriptor->name == 0U &&
            descriptor->first_thunk == 0U) return true;
        if (!pe_string_in_image(image, descriptor->name, &dll_name)) {
            g_pe_error = "nombre de DLL invalido";
            return false;
        }
        lookup_rva = descriptor->original_first_thunk ?
                     descriptor->original_first_thunk : descriptor->first_thunk;
        kprintf("[PE] importando %s\n", dll_name);

        for (;;) {
            uint32_t *lookup;
            uint32_t *iat;
            uint32_t value;
            const char *function_name;
            uint32_t resolved;

            lookup = (uint32_t *)pe_rva_ptr(
                image, lookup_rva + thunk_index * sizeof(uint32_t),
                sizeof(uint32_t));
            iat = (uint32_t *)pe_rva_ptr(
                image, descriptor->first_thunk + thunk_index * sizeof(uint32_t),
                sizeof(uint32_t));
            if (!lookup || !iat) {
                g_pe_error = "tabla de imports fuera de la imagen";
                return false;
            }
            value = *lookup;
            if (value == 0U) break;
            if (value & PE_ORDINAL_FLAG32) {
                uint16_t ordinal = (uint16_t)(value & 0xFFFFU);
                void *module = NULL;
                resolved = win32_resolve_ordinal(dll_name, ordinal);
                if (!resolved) {
                    module = pe_win32_load_library(dll_name);
                    resolved = (uint32_t)(uintptr_t)
                        pe_win32_get_proc_ordinal(module, ordinal);
                }
                if (!resolved) {
                    kprintf("[PE] import ordinal no resuelto: %s!#%u\n",
                            dll_name, ordinal);
                    g_pe_error = "ordinal Win32 no implementado";
                    return false;
                }
                *iat = elf_user_api_thunk("ordinal", resolved);
                kprintf("[PE]   #%u -> %x\n", ordinal, resolved);
                thunk_index++;
                continue;
            }
            if (!pe_range_ok(value, sizeof(uint16_t) + 1U, image->size) ||
                !pe_string_in_image(image, value + sizeof(uint16_t),
                                    &function_name)) {
                g_pe_error = "nombre de funcion importada invalido";
                return false;
            }
            resolved = pe_win32_resolve_export(dll_name, function_name);
            if (!resolved) {
                kprintf("[PE] import no resuelto: %s!%s\n",
                        dll_name, function_name);
                pe_win32_set_last_error(PE_ERROR_CALL_NOT_IMPLEMENTED);
                g_pe_error = "funcion Win32 no implementada";
                return false;
            }
            *iat = elf_user_api_thunk(function_name, resolved);
            kprintf("[PE]   %s -> %x\n", function_name, resolved);
            thunk_index++;
            if (thunk_index > image->size / sizeof(uint32_t)) {
                g_pe_error = "tabla de imports sin terminador";
                return false;
            }
        }
        offset += sizeof(pe_import_descriptor_t);
    }
    g_pe_error = "directorio de imports sin terminador";
    return false;
}

static pe_loaded_image_t *pe_load_image(const uint8_t *file,
                                        uint32_t file_size,
                                        const char *path) {
    const pe_nt_headers32_t *nt;
    const pe_section_header_t *sections;
    pe_loaded_image_t *image;
    uint32_t allocation_size;

    if (!pe_parse_headers(file, file_size, &nt, &sections)) return NULL;
    allocation_size = pe_align_up(nt->optional_header.size_of_image, 0x1000U);
    if (!allocation_size || allocation_size > PE_MAX_IMAGE_SIZE) {
        g_pe_error = "no se pudo alinear SizeOfImage";
        return NULL;
    }
    image = (pe_loaded_image_t *)kzalloc(sizeof(*image));
    if (!image) {
        g_pe_error = "sin memoria para descriptor PE";
        return NULL;
    }
    image->base = (uint8_t *)kzalloc(allocation_size);
    if (!image->base) {
        kfree(image);
        g_pe_error = "sin memoria para imagen PE";
        return NULL;
    }
    image->size = allocation_size;
    image->preferred_base = nt->optional_header.image_base;
    image->entry_rva = nt->optional_header.address_of_entry_point;
    image->is_dll = (nt->file_header.characteristics & 0x2000U) != 0U;
    image->references = 1U;
    if (path) {
        kstrncpy(image->path, path, sizeof(image->path) - 1U);
        image->path[sizeof(image->path) - 1U] = '\0';
    }

    kprintf("[PE] PE32 i386 base preferida=%x base real=%x\n",
            image->preferred_base, (uint32_t)(uintptr_t)image->base);
    kprintf("[PE] entry RVA=%x SizeOfImage=%u secciones=%u\n",
            image->entry_rva, nt->optional_header.size_of_image,
            nt->file_header.number_of_sections);

    if (!pe_map_sections(image, file, file_size, nt, sections) ||
        !pe_apply_relocations(image, nt) ||
        !pe_fix_imports(image, nt)) {
        kfree(image->base);
        kfree(image);
        return NULL;
    }
    return image;
}

static const char *pe_module_basename(const char *name) {
    const char *base = name;
    if (!name) return NULL;
    while (*name) {
        if (*name == '/' || *name == '\\') base = name + 1;
        name++;
    }
    return base;
}

static pe_loaded_image_t *pe_find_module_name(const char *name) {
    const char *base = pe_module_basename(name);
    for (uint32_t i = 0; i < 16U; i++) {
        if (g_pe_modules[i] && pe_ascii_equal_ci(g_pe_modules[i]->module_name, base))
            return g_pe_modules[i];
    }
    return NULL;
}

static pe_loaded_image_t *pe_find_module_handle(void *handle) {
    for (uint32_t i = 0; i < 16U; i++)
        if (g_pe_modules[i] && g_pe_modules[i]->base == (uint8_t *)handle)
            return g_pe_modules[i];
    return NULL;
}

static void pe_call_tls_callbacks(pe_loaded_image_t *image, uint32_t reason) {
    pe_nt_headers32_t *nt;
    pe_data_directory_t directory;
    pe_tls_directory32_t *tls;
    uint32_t *callbacks;
    uint32_t base, end;
    if (!image) return;
    nt = (pe_nt_headers32_t *)(image->base + *(uint32_t *)(image->base + 0x3c));
    directory = nt->optional_header.data_directory[PE_DIRECTORY_TLS];
    if (!directory.virtual_address || directory.size < sizeof(*tls)) return;
    tls = (pe_tls_directory32_t *)pe_rva_ptr(image, directory.virtual_address, sizeof(*tls));
    if (!tls || !tls->address_of_callbacks) return;
    base = (uint32_t)(uintptr_t)image->base; end = base + image->size;
    if (tls->address_of_callbacks < base || tls->address_of_callbacks >= end) return;
    callbacks = (uint32_t *)(uintptr_t)tls->address_of_callbacks;
    for (uint32_t i = 0; i < 64U && callbacks[i]; i++) {
        void (WINAPI *callback)(void *, uint32_t, void *);
        if (callbacks[i] < base || callbacks[i] >= end) break;
        callback = (void (WINAPI *)(void *, uint32_t, void *))(uintptr_t)callbacks[i];
        callback(image->base, reason, NULL);
    }
}

static pe_loaded_image_t *pe_current_process_image(void) {
    uint32_t pid = task_current_process_id();
    pe_loaded_image_t *image = NULL;
    task_preempt_disable();
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        if (g_pe_processes[i].pid == pid) {
            image = g_pe_processes[i].image;
            break;
        }
    }
    task_preempt_enable();
    return image;
}


bool pe_win32_get_image_resource(void *module, const uint8_t **image_out,
                                 uint32_t *image_size_out,
                                 uint32_t *resource_rva_out,
                                 uint32_t *resource_size_out) {
    pe_loaded_image_t *image = pe_current_process_image();
    pe_nt_headers32_t *nt;
    pe_data_directory_t directory;

    if (module && (!image || module != image->base))
        image = pe_find_module_handle(module);
    if (!image || !image->base || image->size < 0x40U) return false;
    uint32_t nt_offset = *(uint32_t *)(image->base + 0x3c);
    if (!pe_range_ok(nt_offset, sizeof(pe_nt_headers32_t), image->size))
        return false;
    nt = (pe_nt_headers32_t *)(image->base + nt_offset);
    if (nt->signature != PE_NT_SIGNATURE ||
        nt->optional_header.magic != PE_OPTIONAL_MAGIC_PE32) return false;
    directory = nt->optional_header.data_directory[PE_DIRECTORY_RESOURCE];
    if (directory.virtual_address &&
        !pe_range_ok(directory.virtual_address, directory.size, image->size))
        return false;
    if (image_out) *image_out = image->base;
    if (image_size_out) *image_size_out = image->size;
    if (resource_rva_out) *resource_rva_out = directory.virtual_address;
    if (resource_size_out) *resource_size_out = directory.size;
    return true;
}

void pe_win32_thread_attach(void) {
    pe_loaded_image_t *image = pe_current_process_image();
    pe_call_tls_callbacks(image, 2U); /* DLL_THREAD_ATTACH */
    for (uint32_t i = 0; i < 16U; i++) {
        int (WINAPI *dll_main)(void *, uint32_t, void *);
        pe_loaded_image_t *module = g_pe_modules[i];
        if (!module) continue;
        pe_call_tls_callbacks(module, 2U);
        if (!module->entry_rva) continue;
        dll_main = (int (WINAPI *)(void *, uint32_t, void *))
            (module->base + module->entry_rva);
        dll_main(module->base, 2U, NULL);
    }
}

void pe_win32_thread_detach(void) {
    pe_loaded_image_t *image = pe_current_process_image();
    for (int i = 15; i >= 0; i--) {
        int (WINAPI *dll_main)(void *, uint32_t, void *);
        pe_loaded_image_t *module = g_pe_modules[i];
        if (!module) continue;
        if (module->entry_rva) {
            dll_main = (int (WINAPI *)(void *, uint32_t, void *))
                (module->base + module->entry_rva);
            dll_main(module->base, 3U, NULL);
        }
        pe_call_tls_callbacks(module, 3U);
    }
    pe_call_tls_callbacks(image, 3U); /* DLL_THREAD_DETACH */
}

static bool pe_install_static_tls(uint32_t pid, pe_loaded_image_t *image) {
    pe_nt_headers32_t *nt;
    pe_data_directory_t directory;
    pe_tls_directory32_t *tls;
    const void *template_data = NULL;
    uint32_t template_size = 0U;
    uint32_t *address_of_index;
    uint32_t base;
    uint32_t end;

    if (!image || !image->base) return false;
    nt = (pe_nt_headers32_t *)(image->base +
        *(uint32_t *)(image->base + 0x3c));
    directory = nt->optional_header.data_directory[PE_DIRECTORY_TLS];
    if (!directory.virtual_address || !directory.size) return true;
    if (directory.size < sizeof(*tls)) {
        g_pe_error = "directorio TLS PE incompleto";
        return false;
    }
    tls = (pe_tls_directory32_t *)pe_rva_ptr(image,
        directory.virtual_address, sizeof(*tls));
    if (!tls || !tls->address_of_index) {
        g_pe_error = "directorio TLS PE invalido";
        return false;
    }

    base = (uint32_t)(uintptr_t)image->base;
    end = base + image->size;
    if (tls->end_raw < tls->start_raw) {
        g_pe_error = "rango de plantilla TLS invalido";
        return false;
    }
    template_size = tls->end_raw - tls->start_raw;
    if (template_size) {
        if (tls->start_raw < base || tls->end_raw > end) {
            g_pe_error = "plantilla TLS fuera de la imagen";
            return false;
        }
        template_data = (const void *)(uintptr_t)tls->start_raw;
    }
    if (tls->address_of_index < base ||
        tls->address_of_index > end - sizeof(uint32_t)) {
        g_pe_error = "AddressOfIndex TLS fuera de la imagen";
        return false;
    }
    address_of_index = (uint32_t *)(uintptr_t)tls->address_of_index;
    if (!win32_process_tls_install(pid, image->base, template_data,
                                   template_size, tls->zero_fill,
                                   address_of_index)) {
        g_pe_error = "no se pudo reservar TLS estatico";
        return false;
    }
    kprintf("[PE] TLS modulo=%x index=%u plantilla=%u zero=%u\n",
            (uint32_t)(uintptr_t)image->base, *address_of_index,
            template_size, tls->zero_fill);
    return true;
}

void *pe_win32_get_module_handle(const char *name) {
    pe_loaded_image_t *image = pe_find_module_name(name);
    return image ? image->base : NULL;
}

void *pe_win32_get_proc_address(void *module, const char *name) {
    pe_loaded_image_t *image = pe_find_module_handle(module);
    pe_nt_headers32_t *nt;
    pe_data_directory_t directory;
    pe_export_directory_t *exports;
    uint32_t *names, *functions;
    uint16_t *ordinals;

    if (!image || !name || (uint32_t)(uintptr_t)name <= 0xFFFFU) return NULL;
    nt = (pe_nt_headers32_t *)(image->base +
        *(uint32_t *)(image->base + 0x3c));
    directory = nt->optional_header.data_directory[PE_DIRECTORY_EXPORT];
    if (!directory.virtual_address || !directory.size) return NULL;
    exports = (pe_export_directory_t *)pe_rva_ptr(image,
        directory.virtual_address, sizeof(*exports));
    if (!exports || exports->number_of_names > image->size / 4U ||
        exports->number_of_functions > image->size / 4U) return NULL;
    names = (uint32_t *)pe_rva_ptr(image, exports->address_of_names,
                                   exports->number_of_names * 4U);
    ordinals = (uint16_t *)pe_rva_ptr(image, exports->address_of_name_ordinals,
                                      exports->number_of_names * 2U);
    functions = (uint32_t *)pe_rva_ptr(image, exports->address_of_functions,
                                       exports->number_of_functions * 4U);
    if (!names || !ordinals || !functions) return NULL;
    for (uint32_t i = 0; i < exports->number_of_names; i++) {
        const char *export_name;
        uint16_t ordinal;
        uint32_t rva;
        if (!pe_string_in_image(image, names[i], &export_name) ||
            !pe_ascii_equal_ci(export_name, name)) continue;
        ordinal = ordinals[i];
        if (ordinal >= exports->number_of_functions) return NULL;
        rva = functions[ordinal];
        /* Un RVA dentro del directorio de exports es un forwarder. */
        if (rva >= directory.virtual_address &&
            rva < directory.virtual_address + directory.size) {
            const char *forwarder;
            char dll[40], proc[64];
            uint32_t d = 0, p = 0;
            if (!pe_string_in_image(image, rva, &forwarder)) return NULL;
            while (forwarder[d] && forwarder[d] != '.' && d < sizeof(dll) - 5U) {
                dll[d] = forwarder[d]; d++;
            }
            if (forwarder[d] != '.') return NULL;
            dll[d] = '\0';
            if (d < 4U || !pe_ascii_equal_ci(&dll[d - 4U], ".DLL")) kstrcat(dll, ".DLL");
            d++;
            while (forwarder[d] && p < sizeof(proc) - 1U) proc[p++] = forwarder[d++];
            proc[p] = '\0';
            if (proc[0] == '#') {
                uint32_t value = 0;
                void *target = pe_win32_load_library(dll);
                for (p = 1; proc[p] >= '0' && proc[p] <= '9'; p++)
                    value = value * 10U + (uint32_t)(proc[p] - '0');
                return pe_win32_get_proc_ordinal(target, (uint16_t)value);
            }
            return (void *)(uintptr_t)pe_win32_resolve_export(dll, proc);
        }
        if (!pe_range_ok(rva, 1U, image->size)) return NULL;
        return image->base + rva;
    }
    return NULL;
}

void *pe_win32_get_proc_ordinal(void *module, uint16_t ordinal) {
    pe_loaded_image_t *image = pe_find_module_handle(module);
    pe_nt_headers32_t *nt;
    pe_data_directory_t directory;
    pe_export_directory_t *exports;
    uint32_t *functions;
    uint32_t index, rva;
    if (!image) return NULL;
    nt = (pe_nt_headers32_t *)(image->base + *(uint32_t *)(image->base + 0x3c));
    directory = nt->optional_header.data_directory[PE_DIRECTORY_EXPORT];
    exports = (pe_export_directory_t *)pe_rva_ptr(image, directory.virtual_address,
                                                   sizeof(*exports));
    if (!exports || ordinal < exports->ordinal_base) return NULL;
    index = (uint32_t)ordinal - exports->ordinal_base;
    if (index >= exports->number_of_functions) return NULL;
    functions = (uint32_t *)pe_rva_ptr(image, exports->address_of_functions,
                                       exports->number_of_functions * 4U);
    if (!functions) return NULL;
    rva = functions[index];
    if (!pe_range_ok(rva, 1U, image->size)) return NULL;
    return image->base + rva;
}

void *pe_win32_load_library(const char *name) {
    pe_loaded_image_t *image;
    void *file = NULL;
    uint32_t file_size = 0;
    char path[VFS_MAX_PATH];
    const char *base;
    int slot = -1;
    int (WINAPI *dll_main)(void *, uint32_t, void *);

    if (!name || !*name) return NULL;
    image = pe_find_module_name(name);
    if (image) { image->references++; return image->base; }
    base = pe_module_basename(name);
    if (name != base || name[0] == '/') {
        kstrncpy(path, name, sizeof(path) - 1U);
    } else {
        kstrcpy(path, "/SYSTEM/LIBS/WINE/");
        if (kstrlen(path) + kstrlen(base) >= sizeof(path)) return NULL;
        kstrcat(path, base);
    }
    path[sizeof(path) - 1U] = '\0';
    if (!vfs_read_all(path, &file, &file_size)) return NULL;
    image = pe_load_image((const uint8_t *)file, file_size, path);
    kfree(file);
    if (!image || !image->is_dll) {
        if (image) { kfree(image->base); kfree(image); }
        return NULL;
    }
    kstrncpy(image->module_name, base, sizeof(image->module_name) - 1U);
    for (uint32_t i = 0; i < 16U; i++) if (!g_pe_modules[i]) { slot = (int)i; break; }
    if (slot < 0) { kfree(image->base); kfree(image); return NULL; }
    g_pe_modules[slot] = image;
    if (win32_process_current_teb() &&
        !pe_install_static_tls(task_current_process_id(), image)) {
        g_pe_modules[slot] = NULL;
        kfree(image->base);
        kfree(image);
        return NULL;
    }
    pe_call_tls_callbacks(image, 1U);
    if (image->entry_rva) {
        dll_main = (int (WINAPI *)(void *, uint32_t, void *))
            (image->base + image->entry_rva);
        if (!dll_main(image->base, 1U, NULL)) {
            pe_call_tls_callbacks(image, 0U);
            win32_process_tls_uninstall(task_current_process_id(), image->base);
            g_pe_modules[slot] = NULL;
            kfree(image->base);
            kfree(image);
            return NULL;
        }
    }
    return image->base;
}

bool pe_win32_free_library(void *module) {
    pe_loaded_image_t *image = pe_find_module_handle(module);
    int (WINAPI *dll_main)(void *, uint32_t, void *);
    if (!image) return false;
    if (image->references) image->references--;
    if (image->references) return true;
    if (image->entry_rva) {
        dll_main = (int (WINAPI *)(void *, uint32_t, void *))
            (image->base + image->entry_rva);
        dll_main(image->base, 0U, NULL); /* DLL_PROCESS_DETACH */
    }
    pe_call_tls_callbacks(image, 0U);
    win32_process_tls_uninstall(task_current_process_id(), image->base);
    for (uint32_t i = 0; i < 16U; i++) if (g_pe_modules[i] == image) {
        g_pe_modules[i] = NULL; break;
    }
    kfree(image->base); kfree(image);
    return true;
}

static bool pe_register_process(uint32_t pid, pe_loaded_image_t *image) {
    bool registered = false;

    task_preempt_disable();
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        if (g_pe_processes[i].pid != 0U) continue;
        g_pe_processes[i].pid = pid;
        g_pe_processes[i].image = image;
        registered = true;
        break;
    }
    task_preempt_enable();
    return registered;
}

static void pe_task_main(void *argument) {
    pe_loaded_image_t *image = (pe_loaded_image_t *)argument;
    void (*entry)(void);
    uint32_t fs_teb = 0U;
    uint32_t fs_peb = 0U;

    if (!image || !image->base || image->entry_rva >= image->size) task_exit();
    entry = (void (*)(void))(uintptr_t)
        ((uint32_t)(uintptr_t)image->base + image->entry_rva);
    __asm__ volatile ("movl %%fs:0x18, %0" : "=r"(fs_teb));
    __asm__ volatile ("movl %%fs:0x30, %0" : "=r"(fs_peb));
    kprintf("[WIN32] TEB=%x PEB=%x FS check=%s\n", fs_teb, fs_peb,
            fs_teb == (uint32_t)(uintptr_t)win32_process_current_teb() &&
            fs_peb == (uint32_t)(uintptr_t)win32_process_current_peb()
                ? "OK" : "FALLO");
    pe_call_tls_callbacks(image, 1U); /* DLL_PROCESS_ATTACH */
    kprintf("[PE] iniciando %s en %x\n",
            image->path[0] ? image->path : "programa Win32",
            (uint32_t)(uintptr_t)entry);
    task_set_memory_hint(image->size);
    entry();
    pe_cleanup_process(task_current_process_id());
    task_exit();
}

bool pe_dump_info(const char *path) {
    void *file = NULL;
    uint32_t file_size = 0;
    const pe_nt_headers32_t *nt;
    const pe_section_header_t *sections;

    if (!path || !path[0]) {
        g_pe_error = "ruta PE vacia";
        return false;
    }
    if (!vfs_read_all(path, &file, &file_size)) {
        g_pe_error = "no se pudo leer el ejecutable PE";
        return false;
    }
    if (!pe_parse_headers((const uint8_t *)file, file_size, &nt, &sections)) {
        kfree(file);
        return false;
    }
    kprintf("[PE] %s: PE32 i386 ImageBase=%x Entry=%x Size=%u\n",
            path, nt->optional_header.image_base,
            nt->optional_header.address_of_entry_point,
            nt->optional_header.size_of_image);
    for (uint16_t i = 0; i < nt->file_header.number_of_sections; i++) {
        char name[9];
        for (uint32_t n = 0; n < 8U; n++) name[n] = (char)sections[i].name[n];
        name[8] = '\0';
        kprintf("[PE]   %s RVA=%x RAW=%u VSIZE=%u\n", name,
                sections[i].virtual_address, sections[i].size_of_raw_data,
                sections[i].virtual_size);
    }
    kfree(file);
    g_pe_error = "sin error";
    return true;
}

bool pe_execute_program_command_line(const char *path,
                                     const char *command_line) {
    void *file = NULL;
    uint32_t file_size = 0;
    pe_loaded_image_t *image;
    int pid;

    if (!path || !path[0]) {
        g_pe_error = "ruta PE vacia";
        return false;
    }
    if (!vfs_read_all(path, &file, &file_size)) {
        g_pe_error = "no se pudo leer el ejecutable PE";
        return false;
    }
    image = pe_load_image((const uint8_t *)file, file_size, path);
    kfree(file);
    if (!image) return false;
    if (image->is_dll) {
        kfree(image->base);
        kfree(image);
        g_pe_error = "una DLL debe cargarse con LoadLibraryA";
        return false;
    }

    /* Evita que el nuevo task sea planificado antes de registrar su imagen. */
    task_preempt_disable();
    pid = task_create_user_program("win32", pe_task_main, image, path);
    if (pid < 0) {
        task_preempt_enable();
        kfree(image->base);
        kfree(image);
        g_pe_error = "sin slots para proceso Win32";
        return false;
    }
    if (!win32_process_create((uint32_t)pid,
                              (uint32_t)(uintptr_t)image->base, path,
                              command_line)) {
        task_request_exit((uint32_t)pid);
        task_preempt_enable();
        kfree(image->base);
        kfree(image);
        g_pe_error = "no se pudo crear PEB/TEB Win32";
        return false;
    }
    if (!pe_install_static_tls((uint32_t)pid, image)) {
        win32_process_destroy((uint32_t)pid);
        task_request_exit((uint32_t)pid);
        task_preempt_enable();
        kfree(image->base);
        kfree(image);
        return false;
    }
    if (!pe_register_process((uint32_t)pid, image)) {
        win32_process_destroy((uint32_t)pid);
        task_request_exit((uint32_t)pid);
        task_preempt_enable();
        kfree(image->base);
        kfree(image);
        g_pe_error = "sin slots internos para proceso PE";
        return false;
    }
    task_preempt_enable();
    g_pe_error = "sin error";
    return true;
}

bool pe_execute_program(const char *path) {
    return pe_execute_program_command_line(path, NULL);
}

const char *pe_last_error(void) {
    return g_pe_error;
}
