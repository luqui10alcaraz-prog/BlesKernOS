#include "include/pe_loader.h"
#include "include/ne_loader.h"
#include "include/elf_loader.h"
#include "include/memory.h"
#include "include/task.h"
#include "include/vfs.h"
#include "include/pit.h"
#include "include/compat_mode.h"
#include "win32/win32.h"
/* BLES_WINE_INSTALL_DIAG_PERF_20260723
 * Los logs por serial dominaban el tiempo de carga de programas con muchos
 * imports. Los fallos siguen visibles; los exitos quedan opcionales. */
#ifndef BLES_WIN32_VERBOSE_PE_IMPORTS
#define BLES_WIN32_VERBOSE_PE_IMPORTS 0
#endif
#ifndef BLES_WIN32_VERBOSE_PE_SECTIONS
#define BLES_WIN32_VERBOSE_PE_SECTIONS 0
#endif
bool win32_wine_stage6_is_data_export(const char *dll, const char *name);
#include "win32/process.h"
#include "win32/resources.h"
#include "stdio.h"

#define PE_DOS_MAGIC                 0x5A4DU
#define PE_NT_SIGNATURE              0x00004550U
#define PE_MACHINE_I386              0x014CU
#define PE_FILE_RELOCS_STRIPPED      0x0001U
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
#define PE_ERROR_MOD_NOT_FOUND 126U
#define PE_ERROR_PROC_NOT_FOUND 127U
#define PE_MAX_MODULES              32U

#define WINAPI __attribute__((stdcall))

/* WIN32_RING3_DLLMAIN_EXACT_V4 */
typedef int (WINAPI *pe_ring3_dllmain_t)(
    void *module, uint32_t reason, void *reserved);

static bool pe_ring3_current_cpl_is_user(void) {
    uint16_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    return (cs & 3U) == 3U;
}

static bool pe_ring3_dllmain_attach(uint32_t entry, void *module) {
    uint32_t arguments[3];

    if (!entry) return true;

    if (pe_ring3_current_cpl_is_user()) {
        pe_ring3_dllmain_t callback =
            (pe_ring3_dllmain_t)(uintptr_t)entry;
        return callback(module, 1U, NULL) != 0;
    }

    arguments[0] = (uint32_t)(uintptr_t)module;
    arguments[1] = 1U;
    arguments[2] = 0U;

    /* BLES_WINE_DLL_DIAGNOSTICS_PE_20260723
     * -5 identifica específicamente un DllMain diferido. No transporta
     * payload; solamente permite que task.c registre ENTER/RETURN. */
    if (!task_queue_user_upcall(
            task_current_pid(), entry, arguments, 3U,
            NULL, 0U, -5)) {
        kprintf("[WIN32] no se pudo encolar DllMain ATTACH "
                "entry=%x pid=%u\n",
                entry, task_current_pid());
        return false;
    }

    kprintf("[WIN32] DllMain ATTACH diferido a Ring 3 "
            "entry=%x pid=%u\n",
            entry, task_current_pid());
    return true;
}

static int pe_ring3_dllmain_detach(uint32_t entry, void *module) {
    if (!entry) return 1;

    if (pe_ring3_current_cpl_is_user()) {
        pe_ring3_dllmain_t callback =
            (pe_ring3_dllmain_t)(uintptr_t)entry;
        return callback(module, 0U, NULL);
    }

    kprintf("[WIN32] DllMain DETACH omitido en Ring 0 "
            "entry=%x modulo=%x\n",
            entry, (uint32_t)(uintptr_t)module);
    return 1;
}


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
    bool fixed_view;
    uint32_t references;
    char module_name[32];
    char path[VFS_MAX_PATH];
} pe_loaded_image_t;

typedef struct {
    uint32_t pid;
    pe_loaded_image_t *image;
} pe_process_slot_t;

static const char *g_pe_error = "sin error";
static char g_pe_loading_directory[VFS_MAX_PATH];
static uint32_t g_win32_last_error; /* fallback before a TEB exists */
static pe_process_slot_t g_pe_processes[TASK_MAX];
static pe_loaded_image_t *g_pe_modules[PE_MAX_MODULES];

/* BLES_WINE_FIXED_VIEW_HANDOFF_EXACT_QUEUE_20260723
 *
 * Wine dispone de un espacio virtual independiente por proceso. BlesKernOS
 * aún comparte la vista fija 0x00400000, por lo que un instalador y la
 * aplicación recién instalada no pueden ocuparla al mismo tiempo.
 */
#define PE_DEFERRED_COMMAND_MAX 512U

typedef struct {
    bool used;
    pe_loaded_image_t *owner_image;
    char path[VFS_MAX_PATH];
    char command_line[PE_DEFERRED_COMMAND_MAX];
} pe_deferred_launch_t;

static pe_deferred_launch_t g_pe_deferred_launch;

/* BLES_WINE_HANDOFF_WORKER_20260723 */
static pe_deferred_launch_t g_pe_deferred_ready;
static uint32_t g_pe_deferred_worker_pid;

static void pe_deferred_launch_worker(void *argument UNUSED) {
    for (;;) {
        char path[VFS_MAX_PATH];
        char command_line[PE_DEFERRED_COMMAND_MAX];
        bool ready = false;

        kmemset(path, 0, sizeof(path));
        kmemset(command_line, 0, sizeof(command_line));

        task_preempt_disable();
        if (g_pe_deferred_ready.used) {
            kstrncpy(path, g_pe_deferred_ready.path, VFS_MAX_PATH - 1U);
            path[VFS_MAX_PATH - 1U] = '\0';
            kstrncpy(command_line, g_pe_deferred_ready.command_line,
                     PE_DEFERRED_COMMAND_MAX - 1U);
            command_line[PE_DEFERRED_COMMAND_MAX - 1U] = '\0';
            kmemset(&g_pe_deferred_ready, 0, sizeof(g_pe_deferred_ready));
            ready = true;
        }
        task_preempt_enable();

        if (ready) {
            uint32_t child_pid = 0U;
            kprintf("[PE] worker handoff: iniciando %s\n", path);
            if (!pe_execute_program_command_line_ex(
                    path, command_line[0] ? command_line : path,
                    &child_pid)) {
                kprintf("[PE] handoff diferido FAIL path=%s error=%s\n",
                        path, pe_last_error());
            } else {
                kprintf("[PE] handoff diferido OK pid=%u path=%s\n",
                        child_pid, path);
            }
        }

        task_sleep(1U);
    }
}

static bool pe_ensure_deferred_worker(void) {
    int pid;
    if (g_pe_deferred_worker_pid) return true;
    pid = task_create_kernel("pe-handoff", pe_deferred_launch_worker, NULL);
    if (pid < 0) return false;
    g_pe_deferred_worker_pid = (uint32_t)pid;
    return true;
}

static bool pe_publish_deferred_launch(const char *path,
                                       const char *command_line) {
    bool published = false;
    if (!path || !path[0]) return false;

    task_preempt_disable();
    if (!g_pe_deferred_ready.used) {
        kmemset(&g_pe_deferred_ready, 0, sizeof(g_pe_deferred_ready));
        g_pe_deferred_ready.used = true;
        kstrncpy(g_pe_deferred_ready.path, path, VFS_MAX_PATH - 1U);
        if (command_line && command_line[0])
            kstrncpy(g_pe_deferred_ready.command_line, command_line,
                     PE_DEFERRED_COMMAND_MAX - 1U);
        published = true;
    }
    task_preempt_enable();
    return published;
}


static bool pe_queue_deferred_launch(pe_loaded_image_t *owner_image,
                                     const char *path,
                                     const char *command_line) {
    bool queued = false;

    
    if (!pe_ensure_deferred_worker()) return false;
if (!owner_image || !path || !path[0]) return false;
    task_preempt_disable();
    if (!g_pe_deferred_launch.used) {
        kmemset(&g_pe_deferred_launch, 0, sizeof(g_pe_deferred_launch));
        g_pe_deferred_launch.used = true;
        g_pe_deferred_launch.owner_image = owner_image;
        kstrncpy(g_pe_deferred_launch.path, path,
                 sizeof(g_pe_deferred_launch.path) - 1U);
        if (command_line && command_line[0])
            kstrncpy(g_pe_deferred_launch.command_line, command_line,
                     sizeof(g_pe_deferred_launch.command_line) - 1U);
        queued = true;
    }
    task_preempt_enable();
    return queued;
}

static bool pe_take_deferred_launch(pe_loaded_image_t *owner_image,
                                    char *path, char *command_line) {
    bool found = false;

    if (!owner_image || !path || !command_line) return false;
    task_preempt_disable();
    if (g_pe_deferred_launch.used &&
        g_pe_deferred_launch.owner_image == owner_image) {
        kstrncpy(path, g_pe_deferred_launch.path, VFS_MAX_PATH - 1U);
        path[VFS_MAX_PATH - 1U] = '\0';
        kstrncpy(command_line, g_pe_deferred_launch.command_line,
                 PE_DEFERRED_COMMAND_MAX - 1U);
        command_line[PE_DEFERRED_COMMAND_MAX - 1U] = '\0';
        kmemset(&g_pe_deferred_launch, 0, sizeof(g_pe_deferred_launch));
        found = true;
    }
    task_preempt_enable();
    return found;
}


/* BLES_WINE_PREFERRED_VIEW_FIX_20260723
 * Wine crea una vista de imagen y primero intenta la dirección solicitada por
 * OptionalHeader.ImageBase. Si no existen relocalizaciones, la imagen no puede
 * moverse. BlesKernOS todavía comparte un único espacio de direcciones, así que
 * reserva una ventana global pequeña únicamente para esos PE no reubicables.
 */
#define PE_FIXED_VIEW_SLOTS 8U

typedef struct {
    pe_loaded_image_t *image;
    uint32_t base;
    uint32_t size;
} pe_fixed_view_slot_t;

static pe_fixed_view_slot_t g_pe_fixed_views[PE_FIXED_VIEW_SLOTS];

static bool pe_has_base_relocations(const pe_nt_headers32_t *nt) {
    pe_data_directory_t dir;
    if (!nt) return false;
    if ((nt->file_header.characteristics & PE_FILE_RELOCS_STRIPPED) != 0U)
        return false;
    dir = nt->optional_header.data_directory[PE_DIRECTORY_BASERELOC];
    return dir.virtual_address != 0U && dir.size != 0U;
}

static bool pe_fixed_ranges_overlap(uint32_t left, uint32_t left_size,
                                    uint32_t right, uint32_t right_size) {
    uint32_t left_end = left + left_size;
    uint32_t right_end = right + right_size;
    return left < right_end && right < left_end;
}

/* BLES_WINE_FIXED_VIEW_HANDOFF_IMAGE_OWNER_20260723 */
static pe_loaded_image_t *pe_fixed_view_owner_for_range(uint32_t base,
                                                        uint32_t size) {
    pe_loaded_image_t *owner = NULL;

    task_preempt_disable();
    for (uint32_t i = 0; i < PE_FIXED_VIEW_SLOTS; i++) {
        if (!g_pe_fixed_views[i].image) continue;
        if (pe_fixed_ranges_overlap(base, size,
                g_pe_fixed_views[i].base, g_pe_fixed_views[i].size)) {
            owner = g_pe_fixed_views[i].image;
            break;
        }
    }
    task_preempt_enable();
    return owner;
}

static bool pe_reserve_fixed_view(pe_loaded_image_t *image,
                                  uint32_t base, uint32_t size) {
    int32_t free_slot = -1;
    uint32_t end;

    if (!image || !size || (base & 0xFFFU) != 0U) return false;
    end = base + size;
    if (end < base || base < PE_FIXED_VIEW_START || end > PE_FIXED_VIEW_END)
        return false;

    task_preempt_disable();
    for (uint32_t i = 0; i < PE_FIXED_VIEW_SLOTS; i++) {
        if (!g_pe_fixed_views[i].image) {
            if (free_slot < 0) free_slot = (int32_t)i;
            continue;
        }
        if (pe_fixed_ranges_overlap(base, size,
                g_pe_fixed_views[i].base, g_pe_fixed_views[i].size)) {
            task_preempt_enable();
            return false;
        }
    }
    if (free_slot < 0) {
        task_preempt_enable();
        return false;
    }

    g_pe_fixed_views[free_slot].image = image;
    g_pe_fixed_views[free_slot].base = base;
    g_pe_fixed_views[free_slot].size = size;
    image->base = (uint8_t *)(uintptr_t)base;
    image->fixed_view = true;
    task_preempt_enable();

    kmemset(image->base, 0, size);
    return true;
}

static void pe_release_fixed_view(pe_loaded_image_t *image) {
    if (!image || !image->fixed_view) return;

    task_preempt_disable();
    for (uint32_t i = 0; i < PE_FIXED_VIEW_SLOTS; i++) {
        if (g_pe_fixed_views[i].image != image) continue;
        g_pe_fixed_views[i].image = NULL;
        g_pe_fixed_views[i].base = 0U;
        g_pe_fixed_views[i].size = 0U;
        break;
    }
    task_preempt_enable();

    if (image->base && image->size) kmemset(image->base, 0, image->size);
    image->base = NULL;
    image->fixed_view = false;
}

static void pe_destroy_image(pe_loaded_image_t *image) {
    uint8_t *heap_base;
    if (!image) return;
    heap_base = image->base;
    if (image->fixed_view) pe_release_fixed_view(image);
    else if (heap_base) kfree(heap_base);
    kfree(image);
}

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
    char deferred_path[VFS_MAX_PATH];
    char deferred_command[PE_DEFERRED_COMMAND_MAX];
    bool run_deferred;

    task_preempt_disable();
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        if (g_pe_processes[i].pid != pid) continue;
        image = g_pe_processes[i].image;
        break;
    }
    task_preempt_enable();

    if (image) pe_call_tls_callbacks(image, 0U);

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

    /*
     * Tomar la orden mientras image todavía identifica al dueño. La ejecución
     * ocurre después de pe_destroy_image(), con 0x00400000 ya libre.
     */
    kmemset(deferred_path, 0, sizeof(deferred_path));
    kmemset(deferred_command, 0, sizeof(deferred_command));
    run_deferred = pe_take_deferred_launch(image, deferred_path,
                                           deferred_command);

    if (image) pe_destroy_image(image);
    if (run_deferred) {
        if (pe_publish_deferred_launch(deferred_path, deferred_command)) {
            kprintf("[PE] handoff fijo: padre pid=%u liberado; encolado %s\n",
                    pid, deferred_path);
        } else {
            kprintf("[PE] handoff diferido DROP path=%s\n", deferred_path);
        }
    }
}

/* BLES_WINE_REAPER_CLEANUP_EXPORT_20260723 */
void pe_win32_cleanup_process(uint32_t process_id) {
    if (!process_id) return;
    pe_cleanup_process(process_id);
}

void pe_win32_terminate_current_process(void) {
    pe_win32_cleanup_process(task_current_process_id());
    task_exit();
}

static void WINAPI pe_k32_ExitProcess(uint32_t exit_code UNUSED) {
    pe_win32_terminate_current_process();
}

/* Do not compare these two import names against string literals.  GCC may
 * suffix-merge "GetLastError" with "WSAGetLastError" (and likewise for
 * SetLastError).  Matching the PE name byte-by-byte keeps this early loader
 * path independent from cross-subsystem string pooling. */
static bool pe_is_get_last_error_name(const char *name) {
    return name && pe_ascii_upper((uint8_t)name[0]) == 'G' &&
           pe_ascii_upper((uint8_t)name[1]) == 'E' &&
           pe_ascii_upper((uint8_t)name[2]) == 'T' &&
           pe_ascii_upper((uint8_t)name[3]) == 'L' &&
           pe_ascii_upper((uint8_t)name[4]) == 'A' &&
           pe_ascii_upper((uint8_t)name[5]) == 'S' &&
           pe_ascii_upper((uint8_t)name[6]) == 'T' &&
           pe_ascii_upper((uint8_t)name[7]) == 'E' &&
           pe_ascii_upper((uint8_t)name[8]) == 'R' &&
           pe_ascii_upper((uint8_t)name[9]) == 'R' &&
           pe_ascii_upper((uint8_t)name[10]) == 'O' &&
           pe_ascii_upper((uint8_t)name[11]) == 'R' && name[12] == '\0';
}

static bool pe_is_set_last_error_name(const char *name) {
    return name && pe_ascii_upper((uint8_t)name[0]) == 'S' &&
           pe_ascii_upper((uint8_t)name[1]) == 'E' &&
           pe_ascii_upper((uint8_t)name[2]) == 'T' &&
           pe_ascii_upper((uint8_t)name[3]) == 'L' &&
           pe_ascii_upper((uint8_t)name[4]) == 'A' &&
           pe_ascii_upper((uint8_t)name[5]) == 'S' &&
           pe_ascii_upper((uint8_t)name[6]) == 'T' &&
           pe_ascii_upper((uint8_t)name[7]) == 'E' &&
           pe_ascii_upper((uint8_t)name[8]) == 'R' &&
           pe_ascii_upper((uint8_t)name[9]) == 'R' &&
           pe_ascii_upper((uint8_t)name[10]) == 'O' &&
             pe_ascii_upper((uint8_t)name[11]) == 'R' && name[12] == '\0';
}

static bool pe_is_read_file_name(const char *name) {
    return name && pe_ascii_upper((uint8_t)name[0]) == 'R' &&
           pe_ascii_upper((uint8_t)name[1]) == 'E' &&
           pe_ascii_upper((uint8_t)name[2]) == 'A' &&
           pe_ascii_upper((uint8_t)name[3]) == 'D' &&
           pe_ascii_upper((uint8_t)name[4]) == 'F' &&
           pe_ascii_upper((uint8_t)name[5]) == 'I' &&
           pe_ascii_upper((uint8_t)name[6]) == 'L' &&
           pe_ascii_upper((uint8_t)name[7]) == 'E' && name[8] == '\0';
}

static bool pe_is_close_handle_name(const char *name) {
    return name && pe_ascii_upper((uint8_t)name[0]) == 'C' &&
           pe_ascii_upper((uint8_t)name[1]) == 'L' &&
           pe_ascii_upper((uint8_t)name[2]) == 'O' &&
           pe_ascii_upper((uint8_t)name[3]) == 'S' &&
           pe_ascii_upper((uint8_t)name[4]) == 'E' &&
           pe_ascii_upper((uint8_t)name[5]) == 'H' &&
           pe_ascii_upper((uint8_t)name[6]) == 'A' &&
           pe_ascii_upper((uint8_t)name[7]) == 'N' &&
           pe_ascii_upper((uint8_t)name[8]) == 'D' &&
           pe_ascii_upper((uint8_t)name[9]) == 'L' &&
           pe_ascii_upper((uint8_t)name[10]) == 'E' && name[11] == '\0';
}

static uint32_t pe_resolve_kernel32(const char *name) {
#define PE_EXPORT(symbol) \
    if (pe_ascii_equal_ci(name, #symbol)) \
        return (uint32_t)(uintptr_t)&pe_k32_##symbol

    if (pe_is_get_last_error_name(name))
        return (uint32_t)(uintptr_t)&pe_k32_GetLastError;
    if (pe_is_set_last_error_name(name))
        return (uint32_t)(uintptr_t)&pe_k32_SetLastError;
    if (pe_is_read_file_name(name))
        return (uint32_t)(uintptr_t)&win32_kernel32_ReadFile;
    if (pe_is_close_handle_name(name))
        return (uint32_t)(uintptr_t)&win32_kernel32_CloseHandle;

    PE_EXPORT(ExitProcess);
    PE_EXPORT(GetCurrentProcessId);
    PE_EXPORT(GetCurrentThreadId);
    PE_EXPORT(GetStdHandle);
    PE_EXPORT(GetTickCount);
    PE_EXPORT(Sleep);
    PE_EXPORT(WriteFile);
#undef PE_EXPORT
    return 0;
}

static uint32_t pe_resolve_builtin_export(const char *dll,const char *name) {
    uint32_t resolved;
    if (pe_ascii_equal_ci(dll, "KERNEL32.DLL") || pe_ascii_equal_ci(dll, "KERNELBASE.DLL")) {
        resolved=pe_resolve_kernel32(name);if(resolved)return resolved;
    }
    return win32_resolve_import(dll,name);
}

uint32_t pe_win32_resolve_export(const char *dll, const char *name) {
    uint32_t resolved = pe_resolve_builtin_export(dll, name);
    void *module;
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
    /* GUI callbacks can be inspected while the compositor is dispatching an
     * event for a different process.  In that case the current PID is not the
     * PE owner, but executing the address in CPL0 would still be fatal. */
    if (!image) {
        for (uint32_t i = 0; i < TASK_MAX; i++) {
            pe_loaded_image_t *candidate = g_pe_processes[i].image;
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
    if (!image) {
        for (uint32_t i = 0; i < PE_MAX_MODULES; i++) {
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
        g_pe_error = "ejecutable DOS MZ de 16 bits: requiere subsistema DOS";
        return false;
    }
    nt = (const pe_nt_headers32_t *)(file + nt_offset);
    if (nt->signature != PE_NT_SIGNATURE) {
        uint16_t signature = (uint16_t)(file[nt_offset] |
                             ((uint16_t)file[nt_offset + 1U] << 8));
        if (signature == 0x454EU)
            g_pe_error = "ejecutable NE/Win16 valido: requiere subsistema Windows 3.x de 16 bits";
        else if (signature == 0x454CU)
            g_pe_error = "ejecutable LE/OS2 de 16 bits no soportado";
        else if (signature == 0x584CU)
            g_pe_error = "ejecutable LX/OS2 no soportado";
        else
            g_pe_error = "ejecutable DOS MZ de 16 bits: requiere subsistema DOS";
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

        for (uint32_t n = 0; n < 8U; n++) name[n] = (char)section->name[n];
        name[8] = '\0';
        if (mapped_size < section->size_of_raw_data)
            mapped_size = section->size_of_raw_data;
        if (!pe_range_ok(section->virtual_address, mapped_size, image->size)) {
            g_pe_error = "seccion fuera de SizeOfImage";
            return false;
        }
        if (section->size_of_raw_data != 0U) {
            uint32_t raw_available = 0U;
            uint32_t raw_missing = 0U;
            if (section->pointer_to_raw_data < file_size)
                raw_available = file_size - section->pointer_to_raw_data;
            if (raw_available > section->size_of_raw_data)
                raw_available = section->size_of_raw_data;
            raw_missing = section->size_of_raw_data - raw_available;
            if (raw_missing) {
                uint32_t alignment = nt->optional_header.file_alignment;
                /* BLES_WINE_TEMP_SFX_FIX_20260723
                 * Some old linkers omit only the zero padding at the
                 * end of the final FileAlignment block.  Windows maps
                 * the available bytes and leaves that tail zeroed.
                 * Never hide a genuinely truncated extracted EXE. */
                if (!raw_available || !alignment || raw_missing >= alignment) {
                    kprintf("[PE] raw fuera: sec=%s ptr=%u raw=%u file=%u "
                            "disponible=%u falta=%u\n",
                            name, section->pointer_to_raw_data,
                            section->size_of_raw_data, file_size,
                            raw_available, raw_missing);
                    g_pe_error = "datos crudos de seccion fuera del archivo";
                    return false;
                }
                kprintf("[PE] padding raw omitido: sec=%s copia=%u cero=%u\n",
                        name, raw_available, raw_missing);
            }
            if (raw_available)
                kmemcpy(image->base + section->virtual_address,
                        file + section->pointer_to_raw_data, raw_available);
        }
        for (uint32_t n = 0; n < 8U; n++) name[n] = (char)section->name[n];
        name[8] = '\0';
#if BLES_WIN32_VERBOSE_PE_SECTIONS
        kprintf("[PE] seccion %s RVA=%x raw=%u virtual=%u\n",
                name, section->virtual_address, section->size_of_raw_data,
                section->virtual_size);
#endif
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

static void *pe_win32_load_library_for_import(const char *name,const char *importer_path);

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
        uint32_t lookup_rva, thunk_index = 0;
        bool builtin;
        void *external_module = NULL;

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
#if BLES_WIN32_VERBOSE_PE_IMPORTS
        kprintf("[PE] importando %s\n", dll_name);
#endif

        builtin = win32_is_builtin_dll(dll_name);
        if (!builtin) {
            external_module = pe_win32_load_library_for_import(dll_name, image->path);
            if (!external_module) {
                kprintf("[PE] DLL requerida no encontrada o no cargable: %s\n",
                        dll_name);
                pe_win32_set_last_error(PE_ERROR_MOD_NOT_FOUND);
                g_pe_error = "DLL Win32 requerida no encontrada";
                return false;
            }
        }
        for (;;) {
            uint32_t *lookup = (uint32_t *)pe_rva_ptr(
                image, lookup_rva + thunk_index * sizeof(uint32_t),
                sizeof(uint32_t));
            uint32_t *iat = (uint32_t *)pe_rva_ptr(
                image, descriptor->first_thunk + thunk_index * sizeof(uint32_t),
                sizeof(uint32_t));
            uint32_t value, resolved = 0;
            const char *function_name;

            if (!lookup || !iat) {
                g_pe_error = "tabla de imports fuera de la imagen";
                return false;
            }
            value = *lookup;
            if (!value) break;
            if (value & PE_ORDINAL_FLAG32) {
                uint16_t ordinal = (uint16_t)(value & 0xffffU);
                resolved = builtin ? win32_resolve_ordinal(dll_name, ordinal) :
                    (uint32_t)(uintptr_t)
                        pe_win32_get_proc_ordinal(external_module, ordinal);
                if (!resolved) {
                    kprintf("[PE] ordinal no exportado: %s!#%u\n",
                            dll_name, ordinal);
                    pe_win32_set_last_error(PE_ERROR_PROC_NOT_FOUND);
                    g_pe_error = builtin ? "ordinal Win32 interno no implementado" :
                                           "ordinal ausente en DLL Win32";
                    return false;
                }
                *iat = elf_user_api_thunk("ordinal", resolved);
#if BLES_WIN32_VERBOSE_PE_IMPORTS
                kprintf("[PE]   #%u -> %x\n", ordinal, resolved);
#endif
            } else {
                if (!pe_range_ok(value, sizeof(uint16_t) + 1U, image->size) ||
                    !pe_string_in_image(image, value + sizeof(uint16_t),
                                        &function_name)) {
                    g_pe_error = "nombre de funcion importada invalido";
                    return false;
                }
                resolved = builtin ? pe_resolve_builtin_export(dll_name, function_name) :
                    (uint32_t)(uintptr_t)
                        pe_win32_get_proc_address(external_module, function_name);
                if (!resolved) {
                    kprintf("[PE] export no resuelto: %s!%s\n",
                            dll_name, function_name);
                    pe_win32_set_last_error(builtin ? PE_ERROR_CALL_NOT_IMPLEMENTED :
                                                     PE_ERROR_PROC_NOT_FOUND);
                    g_pe_error = builtin ? "funcion Win32 interna no implementada" :
                                           "funcion ausente en DLL Win32";
                    return false;
                }
                if (win32_import_is_data(dll_name, function_name)) {
                    *iat = resolved;
                } else {
                    *iat = elf_user_api_thunk(function_name, resolved);
                }
#if BLES_WIN32_VERBOSE_PE_IMPORTS
                kprintf("[PE]   %s -> %x\n", function_name, resolved);
#endif
            }
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

static void pe_directory_from_path(char *output, uint32_t capacity,
                                   const char *path) {
    const char *last = NULL;
    uint32_t length;
    if (!output || capacity == 0U) return;
    output[0] = '\0';
    if (!path) return;
    for (const char *cursor = path; *cursor; cursor++)
        if (*cursor == '/' || *cursor == '\\') last = cursor;
    if (!last) return;
    length = (uint32_t)(last - path);
    if (length == 0U && path[0] == '/') length = 1U;
    if (length >= capacity) length = capacity - 1U;
    for (uint32_t i = 0; i < length; i++) output[i] = path[i];
    output[length] = '\0';
}

static bool pe_current_search_directory(char *output, uint32_t capacity) {
    uint32_t pid;
    if (!output || capacity == 0U) return false;
    output[0] = '\0';
    if (g_pe_loading_directory[0]) {
        kstrncpy(output, g_pe_loading_directory, capacity - 1U);
        output[capacity - 1U] = '\0';
        return true;
    }
    pid = task_current_process_id();
    task_preempt_disable();
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        if (g_pe_processes[i].pid == pid && g_pe_processes[i].image) {
            pe_directory_from_path(output, capacity,
                                   g_pe_processes[i].image->path);
            break;
        }
    }
    task_preempt_enable();
    return output[0] != '\0';
}

static bool pe_join_module_path(char *output, uint32_t capacity,
                                const char *directory, const char *base) {
    uint32_t length;
    if (!output || !capacity || !directory || !base || !*base) return false;
    kstrncpy(output, directory, capacity - 1U);
    output[capacity - 1U] = '\0';
    length = (uint32_t)kstrlen(output);
    if (length && output[length - 1U] != '/') {
        if (length + 1U >= capacity) return false;
        output[length++] = '/';
        output[length] = '\0';
    }
    if (length + kstrlen(base) >= capacity) return false;
    kstrcat(output, base);
    return true;
}

static pe_loaded_image_t *pe_load_image(const uint8_t *file,
                                        uint32_t file_size,
                                        const char *path) {
    const pe_nt_headers32_t *nt;
    const pe_section_header_t *sections;
    pe_loaded_image_t *image;
    uint32_t allocation_size;
    char previous_loading_directory[VFS_MAX_PATH];
    bool loaded;

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
    image->size = allocation_size;
    image->preferred_base = nt->optional_header.image_base;
    if (!pe_has_base_relocations(nt)) {
        if (!pe_reserve_fixed_view(image, image->preferred_base,
                                   allocation_size)) {
            kfree(image);
            g_pe_error =
                "PE sin .reloc: ImageBase no disponible en la vista fija";
            return NULL;
        }
        kprintf("[PE] vista fija sin .reloc: %x-%x\n",
                image->preferred_base,
                image->preferred_base + allocation_size);
    } else {
        image->base = (uint8_t *)kzalloc(allocation_size);
        if (!image->base) {
            kfree(image);
            g_pe_error = "sin memoria para imagen PE";
            return NULL;
        }
    }
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

    kstrncpy(previous_loading_directory, g_pe_loading_directory,
             sizeof(previous_loading_directory) - 1U);
    previous_loading_directory[sizeof(previous_loading_directory) - 1U] = '\0';
    pe_directory_from_path(g_pe_loading_directory,
                           sizeof(g_pe_loading_directory), path);
    /* PE_LOAD_STAGES_EXACT_V4 */
    kprintf("[PE] etapa MAP BEGIN base=%x\n",
            (uint32_t)(uintptr_t)image->base);
    loaded = pe_map_sections(image, file, file_size, nt, sections);

    if (loaded) {
        kprintf("[PE] etapa MAP OK base=%x\n",
                (uint32_t)(uintptr_t)image->base);
        kprintf("[PE] etapa RELOC BEGIN base=%x\n",
                (uint32_t)(uintptr_t)image->base);
        loaded = pe_apply_relocations(image, nt);
    }

    if (loaded) {
        kprintf("[PE] etapa RELOC OK base=%x\n",
                (uint32_t)(uintptr_t)image->base);
        kprintf("[PE] etapa IMPORTS BEGIN base=%x\n",
                (uint32_t)(uintptr_t)image->base);
        loaded = pe_fix_imports(image, nt);
    }

    if (loaded) {
        kprintf("[PE] etapa IMPORTS OK base=%x\n",
                (uint32_t)(uintptr_t)image->base);
    }

    kstrncpy(g_pe_loading_directory, previous_loading_directory,
             sizeof(g_pe_loading_directory) - 1U);
    g_pe_loading_directory[sizeof(g_pe_loading_directory) - 1U] = '\0';

    if (!loaded) {
        kprintf("[PE] carga fallida: %s\n", g_pe_error);
        pe_destroy_image(image);
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

/* WIN32_BUILTIN_DISK_BLOCK */
static bool pe_is_builtin_system_module(const char *name) {
    static const char *builtins[] = {
        "NTDLL.DLL",
        "KERNEL32.DLL",
        "KERNELBASE.DLL",
        "USER32.DLL",
        "GDI32.DLL",
        "MSVCRT.DLL",
        "ADVAPI32.DLL",
        "SHELL32.DLL",
        "COMCTL32.DLL",
        "COMDLG32.DLL",
        "RICHED20.DLL",
        "RICHED32.DLL",
        "OLE32.DLL",
        "OLEAUT32.DLL",
        "RPCRT4.DLL",
        "WINMM.DLL",
        "WININET.DLL",
        "IMM32.DLL",
        "VERSION.DLL",
        "LZ32.DLL",
        "SHLWAPI.DLL",
        "WS2_32.DLL",
        "WSOCK32.DLL",
        "WINSPOOL.DRV"
    };
    const char *base = pe_module_basename(name);

    if (!base || !*base) return false;

    for (uint32_t i = 0;
         i < sizeof(builtins) / sizeof(builtins[0]);
         i++) {
        if (pe_ascii_equal_ci(base, builtins[i])) return true;
    }

    return false;
}

static bool pe_library_path(const char *name, char *path, uint32_t capacity,
                            void **file, uint32_t *file_size) {
    static const char *roots[] = {
        /* External Wine-compatible PE libraries have priority over optional
         * generic runtimes, while built-in Win32 modules are still resolved
         * by pe_builtin_symbol() before this path search is reached. */
        "/SYSTEM/LIBS/WINE/", "/SYSTEM/LIBS/WIN32/", "/SYSTEM/WIN32/"
    };
    const char *base = pe_module_basename(name);
    const char *image_path = win32_process_current_image_path();
    bool explicit_path = false;
    uint32_t length = 0U;
    if (!name || !base || !path || !capacity || !file || !file_size) return false;

    /* WIN32_BUILTIN_PATH_GUARD */
    if (pe_is_builtin_system_module(name)) {
        g_pe_error = "modulo Win32 builtin no se carga desde disco";
        kprintf("[PE] modulo builtin bloqueado en busqueda: %s\n", name);
        return false;
    }

    for (const char *p = name; *p; p++)
        if (*p == '/' || *p == '\\' || *p == ':') explicit_path = true;
    if (explicit_path) {
        uint32_t source = (name[0] && name[1] == ':') ? 2U : 0U;
        while (name[source] && length + 1U < capacity) {
            path[length++] = name[source] == '\\' ? '/' : name[source];
            source++;
        }
        path[length] = '\0';
        if (name[source] == '\0' && vfs_read_all(path, file, file_size)) return true;
    }
    if (image_path && *image_path) {
        const char *slash = image_path;
        for (const char *p = image_path; *p; p++) if (*p == '/') slash = p;
        length = (uint32_t)(slash - image_path + 1);
        if (length + kstrlen(base) < capacity) {
            kmemcpy(path, image_path, length);
            path[length] = '\0';
            kstrcat(path, base);
            if (vfs_read_all(path, file, file_size)) return true;
        }
    }
    for (uint32_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        if (kstrlen(roots[i]) + kstrlen(base) >= capacity) continue;
        kstrcpy(path, roots[i]);
        kstrcat(path, base);
        if (vfs_read_all(path, file, file_size)) return true;
    }
    return false;
}

static pe_loaded_image_t *pe_find_module_name(const char *name) {
    const char *base = pe_module_basename(name);
    for (uint32_t i = 0; i < PE_MAX_MODULES; i++) {
        if (g_pe_modules[i] && pe_ascii_equal_ci(g_pe_modules[i]->module_name, base))
            return g_pe_modules[i];
    }
    return NULL;
}

static pe_loaded_image_t *pe_find_module_handle(void *handle) {
    for (uint32_t i = 0; i < PE_MAX_MODULES; i++)
        if (g_pe_modules[i] && g_pe_modules[i]->base == (uint8_t *)handle)
            return g_pe_modules[i];
    return NULL;
}

static void pe_call_tls_callbacks(pe_loaded_image_t *image, uint32_t reason) {
    /* WIN32_TLS_CPL0_GUARD_EXACT_V4 */
    if (!pe_ring3_current_cpl_is_user()) {
        kprintf("[WIN32] TLS callback omitido en Ring 0 "
                "modulo=%x reason=%u\n",
                image ? (uint32_t)(uintptr_t)image->base : 0U,
                reason);
        return;
    }

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


/* Queue process TLS callbacks before the PE entry runs. The scheduler delivers
 * these as normal Ring-3 upcalls, so no supervisor-only kernel text is ever
 * executed at CPL3. */
static bool pe_queue_tls_callbacks_for_pid(uint32_t pid,
                                           pe_loaded_image_t *image,
                                           uint32_t reason) {
    pe_nt_headers32_t *nt;
    pe_data_directory_t directory;
    pe_tls_directory32_t *tls;
    uint32_t *callbacks;
    uint32_t base, end;
    uint32_t arguments[3];

    if (!pid || !image) return false;
    nt = (pe_nt_headers32_t *)(image->base +
        *(uint32_t *)(image->base + 0x3c));
    directory = nt->optional_header.data_directory[PE_DIRECTORY_TLS];
    if (!directory.virtual_address || directory.size < sizeof(*tls))
        return true;
    tls = (pe_tls_directory32_t *)pe_rva_ptr(
        image, directory.virtual_address, sizeof(*tls));
    if (!tls || !tls->address_of_callbacks) return true;
    base = (uint32_t)(uintptr_t)image->base;
    end = base + image->size;
    if (tls->address_of_callbacks < base ||
        tls->address_of_callbacks >= end) return false;
    callbacks = (uint32_t *)(uintptr_t)tls->address_of_callbacks;
    arguments[0] = base;
    arguments[1] = reason;
    arguments[2] = 0U;
    for (uint32_t i = 0U; i < 64U && callbacks[i]; i++) {
        if (callbacks[i] < base || callbacks[i] >= end) return false;
        if (!task_queue_user_upcall(pid, callbacks[i], arguments, 3U,
                                    NULL, 0U, -1)) {
            kprintf("[PE] cola TLS llena pid=%u callback=%x indice=%u\n",
                    pid, callbacks[i], i);
            return false;
        }
    }
    return true;
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
    for (uint32_t i = 0; i < PE_MAX_MODULES; i++) {
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
            kprintf("[PE:GETPROC] FORWARD module=%s name=%s target=%s\n",
                    image->module_name, name, forwarder);
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
        if (!pe_range_ok(rva, 1U, image->size)) {
            kprintf("[PE:GETPROC] INVALID module=%s handle=%x name=%s rva=%x size=%u\n",
                    image->module_name,
                    (uint32_t)(uintptr_t)module,
                    name, rva, image->size);
            return NULL;
        }
        kprintf("[PE:GETPROC] OK module=%s handle=%x name=%s ordinal=%u rva=%x addr=%x\n",
                image->module_name,
                (uint32_t)(uintptr_t)module,
                name,
                (uint32_t)exports->ordinal_base + (uint32_t)ordinal,
                rva,
                (uint32_t)(uintptr_t)(image->base + rva));

        /* BLES_WINE_WZ56_DEEP_DIAG_20260723
         *
         * WZ56 is WinZip's private compatibility/version probe. Dump enough
         * bytes to identify whether it is a constant-return function, a jump
         * thunk, or a normal routine, without modifying the loaded DLL.
         */
        if (pe_ascii_equal_ci(image->module_name, "wz32.dll") &&
            pe_ascii_equal_ci(name, "WZ56")) {
            const uint8_t *code = image->base + rva;
            uint32_t available = image->size - rva;
            uint32_t count = available < 48U ? available : 48U;

            kprintf("[PE:WZ56] image=%x size=%u entry=%x rva=%x addr=%x bytes=%u\n",
                    (uint32_t)(uintptr_t)image->base,
                    image->size,
                    image->entry_rva,
                    rva,
                    (uint32_t)(uintptr_t)code,
                    count);

            /* BLES_WINE_WZ56_BYTE_DUMP_FIX_20260723
             * kprintf de BlesKernOS no soporta anchos como %02x. */
            for (uint32_t byte = 0U; byte < count; byte += 8U) {
                uint32_t left = count - byte;
                kprintf("[PE:WZ56] +%x:", byte);
                for (uint32_t column = 0U;
                     column < 8U && column < left;
                     column++)
                    kprintf(" %x", (uint32_t)code[byte + column]);
                kprintf("\n");
            }

            if (count >= 6U && code[0] == 0xB8U && code[5] == 0xC3U) {
                uint32_t immediate =
                    (uint32_t)code[1] |
                    ((uint32_t)code[2] << 8) |
                    ((uint32_t)code[3] << 16) |
                    ((uint32_t)code[4] << 24);
                kprintf("[PE:WZ56] patrón MOV EAX,%x; RET detectado\n",
                        immediate);
            } else if (count >= 3U &&
                       code[0] == 0x33U && code[1] == 0xC0U &&
                       code[2] == 0xC3U) {
                kprintf("[PE:WZ56] patrón XOR EAX,EAX; RET detectado\n");
            } else if (count >= 5U && code[0] == 0xE9U) {
                int32_t displacement =
                    (int32_t)((uint32_t)code[1] |
                    ((uint32_t)code[2] << 8) |
                    ((uint32_t)code[3] << 16) |
                    ((uint32_t)code[4] << 24));
                kprintf("[PE:WZ56] JMP relativo -> %x\n",
                        (uint32_t)(uintptr_t)(code + 5 + displacement));
            }
        }

        return image->base + rva;
    }
    kprintf("[PE:GETPROC] MISS module=%s handle=%x name=%s exports=%u\n",
            image->module_name,
            (uint32_t)(uintptr_t)module,
            name, exports->number_of_names);
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

/* BLES_WINE_DLL_WINPATH_FIX_20260723
 *
 * LoadLibraryA puede recibir rutas Win32 absolutas, por ejemplo
 * C:\WinZip\WZCAB2.DLL. El VFS de BlesKernOS espera /WinZip/WZCAB2.DLL.
 * Los imports por nombre simple siguen usando la búsqueda normal junto al
 * ejecutable y en /SYSTEM.
 */
static bool pe_normalize_library_path(const char *input, char *output,
                                      uint32_t capacity) {
    uint32_t source = 0U;
    uint32_t destination = 0U;

    if (!input || !input[0] || !output || capacity < 2U) return false;

    if (((input[0] >= 'A' && input[0] <= 'Z') ||
         (input[0] >= 'a' && input[0] <= 'z')) &&
        input[1] == ':') {
        source = 2U;
    }

    if (input[source] != '/' && input[source] != '\\') {
        output[destination++] = '/';
    }

    while (input[source] && destination + 1U < capacity) {
        char value = input[source++];

        if (value == '\\') value = '/';

        /* Evitar barras dobles al quitar "C:". */
        if (value == '/' && destination > 0U &&
            output[destination - 1U] == '/')
            continue;

        output[destination++] = value;
    }

    output[destination] = '\0';
    return input[source] == '\0' && destination > 0U;
}

static bool pe_library_candidate(const char *path, void **file, uint32_t *size) {
    /* WIN32_BUILTIN_CANDIDATE_GUARD */
    if (!path || !*path || !file || !size) return false;

    if (pe_is_builtin_system_module(path)) {
        g_pe_error = "modulo Win32 builtin no se carga desde disco";
        kprintf("[PE] DLL builtin bloqueada en disco: %s\n", path);
        return false;
    }

    return vfs_read_all(path, file, size);
}

static void *pe_win32_load_library_for_import(const char *name,
                                               const char *importer_path) {
    pe_loaded_image_t *image;
    void *file = NULL;
    uint32_t file_size = 0;
    char path[VFS_MAX_PATH];
    const char *base;
    int slot = -1;
    int (WINAPI *dll_main)(void *, uint32_t, void *);

    if (!name || !*name) return NULL;
    image = pe_find_module_name(name);
    if (image) {
        image->references++;
        return image->base;
    }
    base = pe_module_basename(name);
    if (name != base || name[0] == '/') {
        if (pe_normalize_library_path(name, path, sizeof(path))) {
            kprintf("[PE] LoadLibrary ruta Win32: %s -> %s\n", name, path);
            (void)pe_library_candidate(path, &file, &file_size);
        }
    } else {
        if (importer_path && *importer_path) {
            const char *slash = NULL;
            for (const char *cursor = importer_path; *cursor; cursor++)
                if (*cursor == '/' || *cursor == '\\') slash = cursor;
            if (slash) {
                uint32_t prefix = (uint32_t)(slash - importer_path) + 1U;
                if (prefix + kstrlen(base) < sizeof(path)) {
                    kmemcpy(path, importer_path, prefix);
                    path[prefix] = 0;
                    kstrcat(path, base);
                    (void)pe_library_candidate(path, &file, &file_size);
                }
            }
        }
        if (!file) {
            kstrcpy(path, "/SYSTEM/LIBS/WINE/");
            if (kstrlen(path) + kstrlen(base) < sizeof(path)) {
                kstrcat(path, base);
                (void)pe_library_candidate(path, &file, &file_size);
            }
        }
        if (!file) {
            kstrcpy(path, "/SYSTEM/WIN32/");
            if (kstrlen(path) + kstrlen(base) < sizeof(path)) {
                kstrcat(path, base);
                (void)pe_library_candidate(path, &file, &file_size);
            }
        }
    }
    if (!file) return NULL;
    image = pe_load_image((const uint8_t *)file, file_size, path);
    kfree(file);
    if (!image || !image->is_dll) {
        if (image) {
            pe_destroy_image(image);
        }
        return NULL;
    }
    kstrncpy(image->module_name, base, sizeof(image->module_name) - 1U);
    for (uint32_t i = 0; i < 16U; i++) {
        if (!g_pe_modules[i]) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        pe_destroy_image(image);
        return NULL;
    }
    g_pe_modules[slot] = image;
    if (win32_process_current_teb() &&
        !pe_install_static_tls(task_current_process_id(), image)) {
        g_pe_modules[slot] = NULL;
        pe_destroy_image(image);
        return NULL;
    }
    pe_call_tls_callbacks(image, 1U);
    if (image->entry_rva) {
        dll_main = (int (WINAPI *)(void *, uint32_t, void *))
            (image->base + image->entry_rva);
        if (!pe_ring3_dllmain_attach(
                (uint32_t)(uintptr_t)dll_main, image->base)) {
            pe_call_tls_callbacks(image, 0U);
            win32_process_tls_uninstall(task_current_process_id(), image->base);
            g_pe_modules[slot] = NULL;
            pe_destroy_image(image);
            return NULL;
        }
    }
    kprintf("[PE] DLL externa cargada: %s desde %s\n", base, path);
    return image->base;
}

void *pe_win32_load_library(const char *name) {
    return pe_win32_load_library_for_import(name, NULL);
}

bool pe_win32_free_library(void *module) {
    pe_loaded_image_t *image = pe_find_module_handle(module);
    int (WINAPI *dll_main)(void *, uint32_t, void *);
    if (!image) return false;

    if (pe_ascii_equal_ci(image->module_name, "wz32.dll")) {
        kprintf("[PE:WZ56] FreeLibrary wz32 handle=%x refs=%u base=%x size=%u\n",
                (uint32_t)(uintptr_t)module,
                image->references,
                (uint32_t)(uintptr_t)image->base,
                image->size);
    }
    if (image->references) image->references--;
    if (image->references) return true;
    if (image->entry_rva) {
        dll_main = (int (WINAPI *)(void *, uint32_t, void *))
            (image->base + image->entry_rva);
        (void)pe_ring3_dllmain_detach(
            (uint32_t)(uintptr_t)dll_main, image->base);
        /* DLL_PROCESS_DETACH seguro */
    }
    pe_call_tls_callbacks(image, 0U);
    win32_process_tls_uninstall(task_current_process_id(), image->base);
    for (uint32_t i = 0; i < PE_MAX_MODULES; i++) if (g_pe_modules[i] == image) {
        g_pe_modules[i] = NULL; break;
    }
    pe_destroy_image(image);
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
    if (ne_is_image((const uint8_t *)file, file_size)) {
        bool result = ne_dump_image((const uint8_t *)file, file_size, path);
        if (!result) g_pe_error = ne_last_error();
        kfree(file);
        return result;
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

bool pe_execute_program_command_line_ex(const char *path,
                                        const char *command_line,
                                        uint32_t *process_id_out) {
    void *file = NULL;
    uint32_t file_size = 0;
    pe_loaded_image_t *image;
    task_entry_t user_entry;
    int pid;

    if (process_id_out) *process_id_out = 0U;
    if (!compat_mode_allow_pe()) {
        g_pe_error = "PE/Win16 desactivado en modo de memoria reducida";
        return false;
    }
    if (!path || !path[0]) {
        g_pe_error = "ruta PE vacia";
        return false;
    }
    if (!vfs_read_all(path, &file, &file_size)) {
        g_pe_error = "no se pudo leer el ejecutable PE";
        return false;
    }
    if (ne_is_image((const uint8_t *)file, file_size)) {
        bool result = ne_execute_image((const uint8_t *)file, file_size, path);
        if (!result) g_pe_error = ne_last_error();
        kfree(file);
        return result;
    }
    image = pe_load_image((const uint8_t *)file, file_size, path);
    kfree(file);
    if (!image) {
        /* BLES_WINE_FIXED_VIEW_HANDOFF_EXACT_EXEC_20260723 */
        if (g_pe_error &&
            kstrcmp(g_pe_error,
                "PE sin .reloc: ImageBase no disponible en la vista fija") == 0) {
            pe_loaded_image_t *owner =
                pe_fixed_view_owner_for_range(0x00400000U, 0x1000U);

            if (owner &&
                pe_queue_deferred_launch(owner, path, command_line)) {
                if (process_id_out) *process_id_out = 0U;
                g_pe_error = "lanzamiento PE fijo diferido";
                kprintf("[PE] vista fija ocupada; diferido image=%x path=%s\n",
                        (uint32_t)(uintptr_t)owner, path);
                return true;
            }
        }
        return false;
    }
    if (image->is_dll) {
        pe_destroy_image(image);
        g_pe_error = "una DLL debe cargarse con LoadLibraryA";
        return false;
    }
    if (!image->entry_rva || image->entry_rva >= image->size) {
        pe_destroy_image(image);
        g_pe_error = "punto de entrada PE invalido";
        return false;
    }
    user_entry = (task_entry_t)(uintptr_t)
        ((uint32_t)(uintptr_t)image->base + image->entry_rva);

    /* BLES_WINE_PROCESS_RUNTIME_PREPARE_CALL_20260723
     * No entre a VFS/FAT después de task_preempt_disable(): el worker de
     * handoff corre en una tarea normal, pero el bloqueo de preempción es
     * global y congelaría GUI, mouse y scheduler. */
    if (!win32_process_prepare_runtime()) {
        pe_destroy_image(image);
        g_pe_error = "no se pudo preparar el entorno Win32";
        return false;
    }

    /* Evita que el nuevo task sea planificado antes de registrar su imagen. */
    kprintf("[PE:spawn] PREEMPT DISABLE path=%s\\n", path);
    task_preempt_disable();

    kprintf("[PE:spawn] TASK CREATE BEGIN path=%s entry=%x\n",
            path, (uint32_t)(uintptr_t)user_entry);
    /* Start at the PE image itself. The old pe_task_main lived in protected
     * kernel text and was therefore an invalid CPL3 entry after Phase 1. */
    pid = task_create_user_program("win32", user_entry, NULL, path);
    kprintf("[PE:spawn] TASK CREATE END pid=%d\\n", pid);
    if (pid < 0) {
        task_preempt_enable();
        pe_destroy_image(image);
        g_pe_error = "sin slots para proceso Win32";
        return false;
    }
    kprintf("[PE:spawn] PROCESS CREATE BEGIN pid=%u\\n", (uint32_t)pid);
    if (!win32_process_create((uint32_t)pid,
                              (uint32_t)(uintptr_t)image->base, path,
                              command_line)) {
        task_request_exit((uint32_t)pid);
        task_preempt_enable();
        pe_destroy_image(image);
        g_pe_error = "no se pudo crear PEB/TEB Win32";
        return false;
    }
    kprintf("[PE:spawn] PROCESS CREATE END pid=%u\\n", (uint32_t)pid);
    kprintf("[PE:spawn] STATIC TLS BEGIN pid=%u\\n", (uint32_t)pid);
    if (!pe_install_static_tls((uint32_t)pid, image)) {
        win32_process_destroy((uint32_t)pid);
        task_request_exit((uint32_t)pid);
        task_preempt_enable();
        pe_destroy_image(image);
        return false;
    }
    kprintf("[PE:spawn] STATIC TLS END pid=%u\\n", (uint32_t)pid);
    kprintf("[PE:spawn] REGISTER BEGIN pid=%u\\n", (uint32_t)pid);
    if (!pe_register_process((uint32_t)pid, image)) {
        win32_process_destroy((uint32_t)pid);
        task_request_exit((uint32_t)pid);
        task_preempt_enable();
        pe_destroy_image(image);
        g_pe_error = "sin slots internos para proceso PE";
        return false;
    }
    kprintf("[PE:spawn] REGISTER END pid=%u\n", (uint32_t)pid);
    if (!pe_queue_tls_callbacks_for_pid((uint32_t)pid, image, 1U)) {
        task_request_exit((uint32_t)pid);
        task_preempt_enable();
        g_pe_error = "no se pudieron encolar callbacks TLS";
        return false;
    }
    task_preempt_enable();
    kprintf("[PE:spawn] PREEMPT ENABLE pid=%u\\n", (uint32_t)pid);
    if (process_id_out) *process_id_out = (uint32_t)pid;
    g_pe_error = "sin error";
    return true;
}

bool pe_execute_program_command_line(const char *path, const char *command_line) {
    return pe_execute_program_command_line_ex(path, command_line, NULL);
}

bool pe_execute_program(const char *path) {
    return pe_execute_program_command_line_ex(path, NULL, NULL);
}

const char *pe_last_error(void) {
    return g_pe_error;
}
