#include "../include/file_dialog.h"
#include "../include/vision_ipc.h"
#include "../include/vision_resources.h"
#include "../include/system_sounds.h"
#include "../include/perfmon.h"
#include "../include/elf_loader.h"
#include "../include/memory.h"
#include "../include/wine_policy.h"
#include "../include/paging.h"
#include "../include/vfs.h"
#include "../include/about_dialog.h"
#include "../include/startup_sound.h"
#include "../include/bootsplash.h"
#include "../include/task.h"
#include "../include/native_memory.h"
#include "../include/paging.h"
#include "../include/kernel_domains.h"
#include "../include/klock.h"
#include "../include/gdt.h"
#include "../include/pit.h"
#include "../include/compat_mode.h"
#include "../include/keyboard.h"
#include "../include/block.h"
#include "../include/pci.h"
#include "../include/iso9660.h"
#include "../include/shell.h"
#include "../include/sound.h"
#include "../include/vga.h"
#include "../include/datetime_prefs.h"
#include "../../gui/gui.h"
#include "../../gui/image.h"
#include "../../programs/programs.h"
#include "../libc/include/stdio.h"
#include "../libc/include/stdlib.h"
#include "../libc/include/string.h"
#include "../libc/include/strings.h"
#include "../libc/include/ctype.h"
#include "../libc/include/math.h"

#ifndef ELF_LOAD_TIMING
#define ELF_LOAD_TIMING 0
#endif
#include "../libc/include/errno.h"
#include "../libc/include/sys/stat.h"
#include "../include/rtc.h"
#include "../include/api.h"
#include "../include/ata.h"
#include "../include/driver.h"
#include "../include/usb_core.h"
#include "../include/lpt.h"
#include "../include/gfx_driver.h"
#include "../include/gfx3d_driver.h"
#include "../include/rage128_engine.h"
#include "../include/svga_transport.h"
#include "../include/pic.h"
#include "../include/mouse.h"
#include "../include/vesa.h"
#include "../include/syscall.h"
#include "../include/network.h"

/* api_compat conserva nombres historicos para codigo interno. El cargador debe
 * exportar los simbolos reales de la ABI publica, no expandirlos a drivers. */
#undef bk_proc_cpu_usage
#undef bk_device_block_count
#undef bk_device_pci_count
#undef bk_device_driver_count
extern void bk_console_printf(const char *format, ...);
extern size_t bk_string_length(const char *text);
extern int bk_string_compare(const char *left, const char *right);
extern char *bk_string_copy_n(char *destination, const char *source,
                              size_t capacity);
extern char *bk_string_concat(char *destination, const char *source);
extern int bk_memory_compare(const void *left, const void *right, size_t size);
extern bool bk_proc_launch_arg_copy(char *buffer, uint32_t capacity);
extern uint32_t bk_proc_cpu_usage(void);
extern uint32_t bk_device_block_count(void);
extern bool bk_device_block_info(uint32_t index, bk_block_info_t *info);
extern uint32_t bk_device_pci_count(void);
extern bool bk_device_pci_info(uint32_t index, bk_pci_info_t *info);
extern uint32_t bk_device_driver_count(void);
extern bool bk_device_driver_info(uint32_t index, bk_driver_info_t *info);
extern bool bk_device_volume_info(void *info);
extern bool bk_device_defrag_begin(bool optimize);
extern bool bk_device_defrag_step(void);
extern bool bk_device_defrag_status(void *info);
extern bool bk_device_defrag_cancel(void);
extern bool bk_device_check_volume(void *report);
extern bool bk_device_repair_volume(void *repair, void *after);
extern uint32_t bk_device_partition_count(void);
extern bool bk_device_partition_info(uint32_t index, void *info);
extern bool bk_device_mount_volume(const char *device_name);

#define EI_NIDENT 16
#define ET_REL 1
#define ET_EXEC 2
#define ET_DYN 3
#define EM_386 3
#define EV_CURRENT 1
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_NOBITS 8
#define SHT_REL 9
#define SHF_ALLOC 0x2
#define SHN_UNDEF 0
#define SHN_ABS 0xFFF1
#define STB_WEAK 2
#define ELF32_ST_BIND(INFO) ((INFO) >> 4)
#define R_386_NONE 0
#define R_386_32 1
#define R_386_PC32 2
#define R_386_COPY 5
#define R_386_GLOB_DAT 6
#define R_386_JMP_SLOT 7
#define R_386_RELATIVE 8
#define R_386_TLS_TPOFF 14
#define R_386_TLS_LE 17
#define R_386_TLS_DTPMOD32 35
#define R_386_TLS_DTPOFF32 36
#define R_386_TLS_TPOFF32 37
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_TLS 7
#define DT_NULL 0
#define DT_NEEDED 1
#define DT_PLTRELSZ 2
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_INIT 12
#define DT_FINI 13
#define DT_SONAME 14
#define DT_RPATH 15
#define DT_REL 17
#define DT_RELSZ 18
#define DT_RELENT 19
#define DT_PLTREL 20
#define DT_JMPREL 23
#define DT_INIT_ARRAY 25
#define DT_FINI_ARRAY 26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_RUNPATH 29
#define DT_GNU_HASH 0x6FFFFEF5
#define ELF_LINUX_MODULE_MAX 24U
#define ELF_LINUX_NEEDED_MAX 16U
#define ELF_LINUX_PATH_MAX VFS_MAX_PATH
#define ELF_PROCESS_IMAGE_MAX 128U
#define ELF_USER_THUNK_MAX 1024U
#define ELF_USER_THUNK_SIZE 48U
#define ELF_USER_CALL_WORDS 16U

typedef struct {
    uint8_t ident[EI_NIDENT];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} PACKED elf32_header_t;

typedef struct {
    uint32_t name;
    uint32_t type;
    uint32_t flags;
    uint32_t addr;
    uint32_t offset;
    uint32_t size;
    uint32_t link;
    uint32_t info;
    uint32_t addralign;
    uint32_t entsize;
} PACKED elf32_section_t;

typedef struct {
    uint32_t name;
    uint32_t value;
    uint32_t size;
    uint8_t info;
    uint8_t other;
    uint16_t shndx;
} PACKED elf32_symbol_t;

typedef struct {
    uint32_t offset;
    uint32_t info;
} PACKED elf32_rel_t;

typedef struct {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
} PACKED elf32_program_t;

typedef struct { int32_t tag; uint32_t value; } PACKED elf32_dynamic_t;

typedef struct {
    void *image;
    void *entry;
    /* bias is the address visible to Ring 3; access_bias is the kernel's
     * identity-mapped alias used while linking a fixed ET_EXEC image. */
    uint32_t bias;
    uint32_t access_bias;
    uint32_t low;
    uint32_t high;
    elf32_program_t *programs;
    uint16_t program_count;
    elf32_symbol_t *symbols;
    const char *strings;
    uint32_t string_size;
    uint32_t symbol_count;
    uint32_t rel;
    uint32_t rel_size;
    uint32_t jump_rel;
    uint32_t jump_rel_size;
    uint32_t needed[ELF_LINUX_NEEDED_MAX];
    uint32_t needed_count;
    uint32_t soname_offset;
    uint32_t rpath_offset;
    uint32_t runpath_offset;
    uint32_t init;
    uint32_t init_array;
    uint32_t init_array_size;
    uint32_t tls_vaddr;
    uint32_t tls_file_size;
    uint32_t tls_memory_size;
    uint32_t tls_alignment;
    uint32_t tls_offset;
    uint32_t tls_module_id;
    char path[ELF_LINUX_PATH_MAX];
} elf_linux_module_t;

typedef void (*elf_program_entry_t)(gui_desktop_t *desktop);

static const char *g_elf_error = "sin error";
typedef struct {
    uint32_t process_id;
    void *image;
    void *linux_address_pin;
    uint32_t linux_address, linux_length; /* limites confiables, fuera del ELF usuario */
} elf_process_image_t;

#define ELF_LINUX_SPARSE_LOAD_MAX 16U
typedef struct {
    void *image;
    uint32_t virtual_address;
    uint32_t length;
    bool writable;
} elf_linux_sparse_load_t;

/* Shared objects are images, not tasks.  A GUI binary can easily pull twenty
 * libraries while still consuming a single scheduler slot. */
static elf_process_image_t g_process_images[ELF_PROCESS_IMAGE_MAX];
/* RELRO de main/ld.so: validar contra limites guardados al cargar, nunca
 * contra headers modificables por el programa. Las paginas vecinas no cambian. */
bool elf_linux_protect(uint32_t process_id, uint32_t address,
                        uint32_t length, uint32_t protection) {
    if (!length || (address&4095U) || address+length<address ||
        (protection&~7U)) return false;
    for (uint32_t i=0;i<ELF_PROCESS_IMAGE_MAX;++i) {
        const elf_process_image_t *m=&g_process_images[i];
        if (m->process_id!=process_id || !m->linux_length ||
            address<m->linux_address ||
            address+length>m->linux_address+m->linux_length) continue;
        return paging_process_create(process_id) &&
            paging_process_map(process_id,address,
                (uint32_t)(uintptr_t)m->image+address-m->linux_address,
                length,protection!=0U,(protection&2U)!=0U);
    }
    return false;
}

bool elf_linux_range_occupied(uint32_t process_id, uint32_t address,
                              uint32_t length) {
    uint32_t end;
    if (!process_id || !length || address + length < address) return true;
    end = address + length;
    for (uint32_t i = 0U; i < ELF_PROCESS_IMAGE_MAX; i++) {
        const elf_process_image_t *m = &g_process_images[i];
        uint32_t map_end;
        if (m->process_id != process_id || !m->linux_length) continue;
        map_end = m->linux_address + m->linux_length;
        if (map_end < m->linux_address) return true;
        if (address < map_end && m->linux_address < end) return true;
    }
    return false;
}
/* User-callable thunks cannot live in kernel BSS now that Phase 1 marks
 * kernel pages supervisor-only. Allocate them from the shared user heap and
 * publish each pointer only after the code bytes are complete. */
static uint8_t *g_user_thunks[ELF_USER_THUNK_MAX];
static uint32_t g_user_targets[ELF_USER_THUNK_MAX];
static kspinlock_t g_user_thunk_lock = KSPINLOCK_INITIALIZER;
static uint32_t *g_user_errno_slot;
static uint32_t *g_user_stdout_slot;
static uint32_t *g_user_stderr_slot;
/* Stable metadata for user-callable API thunks.
 *
 * The ELF symbol name passed to elf_user_api_thunk() points into the image
 * currently being relocated.  That image is released when its process exits,
 * while the thunk/token cache is global and survives for the lifetime of the
 * kernel.  Keeping the original pointer therefore turns every later API call
 * into a use-after-free inside kernel_domain_mask_for_api().
 *
 * Classify while the loader string is valid and retain only a bounded label
 * for diagnostics.  Both arrays live in supervisor-only BSS, so Ring 3 cannot
 * rewrite the lock-domain decision. */
#define ELF_USER_API_LABEL_MAX 32U
static uint32_t g_user_api_domains[ELF_USER_THUNK_MAX];
static char g_user_api_labels[ELF_USER_THUNK_MAX][ELF_USER_API_LABEL_MAX];
static const char *g_last_user_api_name = "(ninguna)";
static uint32_t g_user_thunk_count;

bool elf_preview_create(const char *path, gui_desktop_t *desktop,
                        void **image_out);
void elf_preview_destroy(void *image);

extern uint64_t __udivdi3(uint64_t a, uint64_t b);
extern uint64_t __umoddi3(uint64_t a, uint64_t b);
extern int64_t __divdi3(int64_t a, int64_t b);
extern int64_t __moddi3(int64_t a, int64_t b);

static bool elf_range_ok(uint32_t offset, uint32_t length, uint32_t total) {
    return offset <= total && length <= total - offset;
}

static uint32_t elf_align(uint32_t value, uint32_t alignment) {
    if (alignment <= 1) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}


static bool g_loading_vision_service;
extern void screensaver_initialize(gui_desktop_t *desktop);

#define EXPORT(symbol) \
    if (kstrcmp(name, #symbol) == 0) return (uint32_t)(uintptr_t)&symbol

static const char *vision_mount_name_copy(void) {
    return task_user_export_string(vfs_get_mount_name());
}
static const char *vision_vfs_error_copy(void) {
    return task_user_export_string(vfs_last_error_text());
}
static const char *vision_language_expand_copy(const char *source,char *output,uint32_t capacity) {
    const char *result=language_expand(source,output,capacity);
    return result==output?result:task_user_export_string(result);
}
static uint32_t elf_vision_private_symbol(const char *name) {
    /* Policy code reads these strings directly in CPL3. Export a bounded
     * caller-owned copy instead of a supervisor data address. */
    if(kstrcmp(name,"vfs_get_mount_name")==0)return (uint32_t)(uintptr_t)&vision_mount_name_copy;
    if(kstrcmp(name,"vfs_last_error_text")==0)return (uint32_t)(uintptr_t)&vision_vfs_error_copy;
    if(kstrcmp(name,"language_expand")==0)return (uint32_t)(uintptr_t)&vision_language_expand_copy;

#define VISION_PRIVATE_EXPORT(symbol) \
    if (kstrcmp(name, #symbol) == 0) return (uint32_t)(uintptr_t)&symbol

    /* These imports are deliberately NOT part of the normal Ring-3 ABI.
     * They are exposed only while /SYSTEM/SERVICES/VISION.BEX is being
     * relocated. Vision owns desktop policy; ordinary applications keep
     * using the stable bk_gui_* API and the geometry-only desktop proxy. */
    VISION_PRIVATE_EXPORT(bk_vision_take_desktop_preferences);
    VISION_PRIVATE_EXPORT(bk_vision_publish_desktop_preferences);
    VISION_PRIVATE_EXPORT(bk_vision_native_pending);
    VISION_PRIVATE_EXPORT(bk_vision_native_frame);
    VISION_PRIVATE_EXPORT(bootsplash_debug);
    VISION_PRIVATE_EXPORT(bk_graphics_icon_load_size);
    VISION_PRIVATE_EXPORT(bootsplash_pulse);
    VISION_PRIVATE_EXPORT(bootsplash_show);
    VISION_PRIVATE_EXPORT(compat_mode_allow_icon_images);
    VISION_PRIVATE_EXPORT(compat_mode_allow_wallpaper);
    VISION_PRIVATE_EXPORT(compat_mode_is_low_memory);
    VISION_PRIVATE_EXPORT(gui_window_capture_content);
    VISION_PRIVATE_EXPORT(gui_window_begin_content_paint);
    VISION_PRIVATE_EXPORT(gui_window_end_content_paint);
    VISION_PRIVATE_EXPORT(gui_get_desktop);
    VISION_PRIVATE_EXPORT(gui_vision_owner_pid);
    VISION_PRIVATE_EXPORT(bk_vision_driver_window_event);
    VISION_PRIVATE_EXPORT(task_queue_user_upcall_owned);
    VISION_PRIVATE_EXPORT(task_current_process_id);
    VISION_PRIVATE_EXPORT(bk_vision_file_dialog_complete);
    VISION_PRIVATE_EXPORT(bk_vision_file_dialog_pending);
    VISION_PRIVATE_EXPORT(bk_vision_resource_register);
    VISION_PRIVATE_EXPORT(bk_vision_resource_retire);
    VISION_PRIVATE_EXPORT(bk_vision_resource_forget);
    VISION_PRIVATE_EXPORT(bk_vision_resource_owner);
    VISION_PRIVATE_EXPORT(bk_vision_ipc_take);
    VISION_PRIVATE_EXPORT(bk_vision_ipc_reply);
    VISION_PRIVATE_EXPORT(bk_vision_ipc_wait);
    VISION_PRIVATE_EXPORT(bk_vision_worker_begin);
    VISION_PRIVATE_EXPORT(bk_vision_worker_end);
    VISION_PRIVATE_EXPORT(bk_vision_frame_begin);
    VISION_PRIVATE_EXPORT(bk_vision_frame_end);
    VISION_PRIVATE_EXPORT(bk_vision_note_input);
    VISION_PRIVATE_EXPORT(compat_mode_gui_idle_hz);
    VISION_PRIVATE_EXPORT(compat_mode_gui_target_fps);
    VISION_PRIVATE_EXPORT(deskmanager_refresh_layout);
    VISION_PRIVATE_EXPORT(gfx_cursor_define);
    VISION_PRIVATE_EXPORT(gfx_cursor_move);
    VISION_PRIVATE_EXPORT(gfx_cursor_show);
    VISION_PRIVATE_EXPORT(gfx_cursor_supported);
    VISION_PRIVATE_EXPORT(gfx_driver_generation);
    VISION_PRIVATE_EXPORT(gui_desktop_paint_lock);
    VISION_PRIVATE_EXPORT(gui_desktop_paint_unlock);
    VISION_PRIVATE_EXPORT(gui_gfx_output_depth);
    VISION_PRIVATE_EXPORT(gui_gfx_present_rects);
    VISION_PRIVATE_EXPORT(gui_setup_mode);
    VISION_PRIVATE_EXPORT(kmutex_lock);
    VISION_PRIVATE_EXPORT(kmutex_unlock);
    VISION_PRIVATE_EXPORT(language_expand);
    VISION_PRIVATE_EXPORT(mouse_get_state);
    VISION_PRIVATE_EXPORT(mouse_pop_event);
    VISION_PRIVATE_EXPORT(perfmon_gui_event);
    VISION_PRIVATE_EXPORT(perfmon_gui_frame);
    VISION_PRIVATE_EXPORT(perfmon_gui_loop);
    VISION_PRIVATE_EXPORT(system_sound_event_play);
    VISION_PRIVATE_EXPORT(task_current_is_user);
    VISION_PRIVATE_EXPORT(task_queue_user_upcall);
    VISION_PRIVATE_EXPORT(task_set_affinity_mask);
    VISION_PRIVATE_EXPORT(vfs_size);
    VISION_PRIVATE_EXPORT(mm_physical_top);
    VISION_PRIVATE_EXPORT(program_execute_path_async);
    VISION_PRIVATE_EXPORT(program_execute_path_async_arg);
    VISION_PRIVATE_EXPORT(task_queue_window_upcall);
    VISION_PRIVATE_EXPORT(vfs_last_error_text);
    VISION_PRIVATE_EXPORT(vfs_seek);
    VISION_PRIVATE_EXPORT(vfs_stat);
    VISION_PRIVATE_EXPORT(screensaver_initialize);

#undef VISION_PRIVATE_EXPORT
    return 0U;
}

static uint32_t elf_kernel_symbol(const char *name, bool supervisor_image) {
    /* Loader-local image privilege; never a process-global import switch. */
    if (supervisor_image) {
        EXPORT(mm_alloc_guarded_stack);
        EXPORT(mm_free_guarded_stack);
        EXPORT(paging_kernel_mmio_uncached);
        EXPORT(kmutex_lock);
        EXPORT(kmutex_unlock);
    }
    if (g_loading_vision_service) {
        uint32_t private_target = elf_vision_private_symbol(name);
        if (private_target) return private_target;
    }
    /*
     * Compatibility for BEX files produced by Codex/32 2.6.1's first
     * static-libc linker.  build/system/libs/libc/libc.a is the kernel libc
     * archive and its stdio.o imports these two private kernel names.  Never
     * expose the private implementations directly to Ring 3: route them to
     * the public API wrappers instead.  New Codex builds use the SDK POSIX
     * runtime and do not emit these imports.
     */
    if (kstrcmp(name, "language_translate") == 0)
        return (uint32_t)(uintptr_t)&bk_lang_translate;
    if (kstrcmp(name, "vga_putchar") == 0)
        return (uint32_t)(uintptr_t)&bk_console_putchar;

    if(kstrcmp(name,"gui_window_add_menu")==0)return (uint32_t)(uintptr_t)&bk_gui_add_menu;
    if(kstrcmp(name,"gui_window_add_menu_item")==0)return (uint32_t)(uintptr_t)&bk_gui_add_menu_item;
    if(kstrcmp(name,"gui_get_desktop")==0)return (uint32_t)(uintptr_t)&bk_gui_desktop;

    /* Preserve historical client spellings through the message ABI. */
    if(kstrcmp(name,"gui_desktop_create_window") == 0)return (uint32_t)(uintptr_t)&bk_gui_create_window;
    if(kstrcmp(name,"gui_desktop_focus_window") == 0)return (uint32_t)(uintptr_t)&bk_gui_focus_window;
    if(kstrcmp(name,"gui_window_close") == 0)return (uint32_t)(uintptr_t)&bk_gui_close_window;
    if(kstrcmp(name,"gui_window_set_min_size") == 0)return (uint32_t)(uintptr_t)&bk_gui_set_window_min_size;
    if(kstrcmp(name,"gui_window_set_borderless") == 0)return (uint32_t)(uintptr_t)&bk_gui_window_set_borderless;

    /* GCC / runtime */
    EXPORT(__udivdi3);
    EXPORT(__umoddi3);
    EXPORT(__divdi3);
    EXPORT(__moddi3);

    /* libc / stdio / stdlib / string */
    EXPORT(abs);
    EXPORT(atof);
    EXPORT(atoi);
    EXPORT(calloc);
    EXPORT(errno);
    EXPORT(exit);
    EXPORT(fabs);
    EXPORT(atan);
    EXPORT(cos);
    EXPORT(sin);
    EXPORT(tan);
    EXPORT(sqrt);
    EXPORT(floor);
    EXPORT(pow);
    EXPORT(fclose);
    EXPORT(feof);
    EXPORT(ferror);
    EXPORT(fflush);
    EXPORT(fopen);
    EXPORT(fprintf);
    EXPORT(fread);
    EXPORT(free);
    EXPORT(fseek);
    EXPORT(ftell);
    EXPORT(fwrite);
    EXPORT(malloc);
    EXPORT(memcmp);
    EXPORT(memcpy);
    EXPORT(memmove);
    EXPORT(memset);
    EXPORT(mkdir);
    EXPORT(printf);
    EXPORT(putchar);
    EXPORT(puts);
    EXPORT(realloc);
    EXPORT(remove);
    EXPORT(rename);
    EXPORT(snprintf);
    EXPORT(sscanf);

    /*
     * ELF userland compatibility registry.
     *
     * Codex/self-hosted programs can now use the libc/POSIX/SDK symbols
     * already present in the kernel without requiring a one-off patch for
     * every newly used function.  The aliases are weak on purpose: names
     * which are not built in this configuration resolve to address 0 and
     * are simply skipped, so this list can safely cover optional subsystems.
     *
     * Keep the normal EXPORT() entries above: they remain the fast/explicit
     * ABI.  This block is the broad compatibility fallback for SDK programs.
     */
    #define ELF_WEAK_EXPORT(sym) do { \
        extern char __elf_weak_##sym[] __asm__(#sym) __attribute__((weak)); \
        if (strcmp(name, #sym) == 0 && __elf_weak_##sym) \
            return (uint32_t)(uintptr_t)__elf_weak_##sym; \
    } while (0)

    ELF_WEAK_EXPORT(scanf);
    ELF_WEAK_EXPORT(fscanf);
    ELF_WEAK_EXPORT(vscanf);
    ELF_WEAK_EXPORT(vfscanf);
    ELF_WEAK_EXPORT(vsscanf);
    ELF_WEAK_EXPORT(sprintf);
    ELF_WEAK_EXPORT(vsprintf);
    ELF_WEAK_EXPORT(snprintf);
    ELF_WEAK_EXPORT(vsnprintf);
    ELF_WEAK_EXPORT(printf);
    ELF_WEAK_EXPORT(vprintf);
    ELF_WEAK_EXPORT(fprintf);
    ELF_WEAK_EXPORT(vfprintf);
    ELF_WEAK_EXPORT(sscanf);
    ELF_WEAK_EXPORT(puts);
    ELF_WEAK_EXPORT(fputs);
    ELF_WEAK_EXPORT(putchar);
    ELF_WEAK_EXPORT(fputc);
    ELF_WEAK_EXPORT(getchar);
    ELF_WEAK_EXPORT(fgetc);
    ELF_WEAK_EXPORT(fgets);
    ELF_WEAK_EXPORT(fopen);
    ELF_WEAK_EXPORT(freopen);
    ELF_WEAK_EXPORT(fdopen);
    ELF_WEAK_EXPORT(fclose);
    ELF_WEAK_EXPORT(fflush);
    ELF_WEAK_EXPORT(fread);
    ELF_WEAK_EXPORT(fwrite);
    ELF_WEAK_EXPORT(fseek);
    ELF_WEAK_EXPORT(ftell);
    ELF_WEAK_EXPORT(rewind);
    ELF_WEAK_EXPORT(feof);
    ELF_WEAK_EXPORT(ferror);
    ELF_WEAK_EXPORT(clearerr);
    ELF_WEAK_EXPORT(fileno);
    ELF_WEAK_EXPORT(setbuf);
    ELF_WEAK_EXPORT(setvbuf);
    ELF_WEAK_EXPORT(perror);
    ELF_WEAK_EXPORT(tmpfile);
    ELF_WEAK_EXPORT(tmpnam);
    ELF_WEAK_EXPORT(remove);
    ELF_WEAK_EXPORT(rename);
    ELF_WEAK_EXPORT(malloc);
    ELF_WEAK_EXPORT(calloc);
    ELF_WEAK_EXPORT(realloc);
    ELF_WEAK_EXPORT(free);
    ELF_WEAK_EXPORT(abort);
    ELF_WEAK_EXPORT(exit);
    ELF_WEAK_EXPORT(_Exit);
    ELF_WEAK_EXPORT(atexit);
    ELF_WEAK_EXPORT(atoi);
    ELF_WEAK_EXPORT(atol);
    ELF_WEAK_EXPORT(atoll);
    ELF_WEAK_EXPORT(strtol);
    ELF_WEAK_EXPORT(strtoul);
    ELF_WEAK_EXPORT(strtoll);
    ELF_WEAK_EXPORT(strtoull);
    ELF_WEAK_EXPORT(strtof);
    ELF_WEAK_EXPORT(strtod);
    ELF_WEAK_EXPORT(strtold);
    ELF_WEAK_EXPORT(rand);
    ELF_WEAK_EXPORT(srand);
    ELF_WEAK_EXPORT(qsort);
    ELF_WEAK_EXPORT(bsearch);
    ELF_WEAK_EXPORT(abs);
    ELF_WEAK_EXPORT(labs);
    ELF_WEAK_EXPORT(llabs);
    ELF_WEAK_EXPORT(div);
    ELF_WEAK_EXPORT(ldiv);
    ELF_WEAK_EXPORT(lldiv);
    ELF_WEAK_EXPORT(getenv);
    ELF_WEAK_EXPORT(setenv);
    ELF_WEAK_EXPORT(unsetenv);
    ELF_WEAK_EXPORT(putenv);
    ELF_WEAK_EXPORT(system);
    ELF_WEAK_EXPORT(realpath);
    ELF_WEAK_EXPORT(memcpy);
    ELF_WEAK_EXPORT(memmove);
    ELF_WEAK_EXPORT(memset);
    ELF_WEAK_EXPORT(memcmp);
    ELF_WEAK_EXPORT(memchr);
    ELF_WEAK_EXPORT(strlen);
    ELF_WEAK_EXPORT(strnlen);
    ELF_WEAK_EXPORT(strcpy);
    ELF_WEAK_EXPORT(strncpy);
    ELF_WEAK_EXPORT(strcat);
    ELF_WEAK_EXPORT(strncat);
    ELF_WEAK_EXPORT(strcmp);
    ELF_WEAK_EXPORT(strncmp);
    ELF_WEAK_EXPORT(strcasecmp);
    ELF_WEAK_EXPORT(strncasecmp);
    ELF_WEAK_EXPORT(strchr);
    ELF_WEAK_EXPORT(strrchr);
    ELF_WEAK_EXPORT(strstr);
    ELF_WEAK_EXPORT(strdup);
    ELF_WEAK_EXPORT(strndup);
    ELF_WEAK_EXPORT(strtok);
    ELF_WEAK_EXPORT(strtok_r);
    ELF_WEAK_EXPORT(strerror);
    ELF_WEAK_EXPORT(strspn);
    ELF_WEAK_EXPORT(strcspn);
    ELF_WEAK_EXPORT(strpbrk);
    ELF_WEAK_EXPORT(isalnum);
    ELF_WEAK_EXPORT(isalpha);
    ELF_WEAK_EXPORT(isblank);
    ELF_WEAK_EXPORT(iscntrl);
    ELF_WEAK_EXPORT(isdigit);
    ELF_WEAK_EXPORT(isgraph);
    ELF_WEAK_EXPORT(islower);
    ELF_WEAK_EXPORT(isprint);
    ELF_WEAK_EXPORT(ispunct);
    ELF_WEAK_EXPORT(isspace);
    ELF_WEAK_EXPORT(isupper);
    ELF_WEAK_EXPORT(isxdigit);
    ELF_WEAK_EXPORT(tolower);
    ELF_WEAK_EXPORT(toupper);

    /* sin/cos/tan/atan/pow/sqrt/floor/fabs are part of the explicit native
     * ABI above.  Do NOT redeclare those names through ELF_WEAK_EXPORT: the
     * weak asm alias changes the ELF binding of the whole translation unit,
     * so ld no longer pulls kernel/libc/math.o from libc.a.  The resolver then
     * sees address 0 at runtime even though an EXPORT(name) entry exists.
     *
     * Keep only the genuinely optional libm names weak here. */
    ELF_WEAK_EXPORT(asin);
    ELF_WEAK_EXPORT(acos);
    ELF_WEAK_EXPORT(atan2);
    ELF_WEAK_EXPORT(sinh);
    ELF_WEAK_EXPORT(cosh);
    ELF_WEAK_EXPORT(tanh);
    ELF_WEAK_EXPORT(exp);
    ELF_WEAK_EXPORT(exp2);
    ELF_WEAK_EXPORT(log);
    ELF_WEAK_EXPORT(log10);
    ELF_WEAK_EXPORT(log2);
    ELF_WEAK_EXPORT(sqrtf);
    ELF_WEAK_EXPORT(cbrt);
    ELF_WEAK_EXPORT(ceil);
    ELF_WEAK_EXPORT(ceilf);
    ELF_WEAK_EXPORT(floorf);
    ELF_WEAK_EXPORT(trunc);
    ELF_WEAK_EXPORT(round);
    ELF_WEAK_EXPORT(lround);
    ELF_WEAK_EXPORT(llround);
    ELF_WEAK_EXPORT(fabsf);
    ELF_WEAK_EXPORT(fmod);
    ELF_WEAK_EXPORT(remainder);
    ELF_WEAK_EXPORT(modf);
    ELF_WEAK_EXPORT(frexp);
    ELF_WEAK_EXPORT(ldexp);
    ELF_WEAK_EXPORT(copysign);
    ELF_WEAK_EXPORT(hypot);
    ELF_WEAK_EXPORT(open);
    ELF_WEAK_EXPORT(close);
    ELF_WEAK_EXPORT(read);
    ELF_WEAK_EXPORT(write);
    ELF_WEAK_EXPORT(pread);
    ELF_WEAK_EXPORT(pwrite);
    ELF_WEAK_EXPORT(lseek);
    ELF_WEAK_EXPORT(fsync);
    ELF_WEAK_EXPORT(fdatasync);
    ELF_WEAK_EXPORT(ftruncate);
    ELF_WEAK_EXPORT(truncate);
    ELF_WEAK_EXPORT(dup);
    ELF_WEAK_EXPORT(dup2);
    ELF_WEAK_EXPORT(fcntl);
    ELF_WEAK_EXPORT(ioctl);
    ELF_WEAK_EXPORT(pipe);
    ELF_WEAK_EXPORT(poll);
    ELF_WEAK_EXPORT(select);
    ELF_WEAK_EXPORT(opendir);
    ELF_WEAK_EXPORT(readdir);
    ELF_WEAK_EXPORT(closedir);
    ELF_WEAK_EXPORT(rewinddir);
    ELF_WEAK_EXPORT(telldir);
    ELF_WEAK_EXPORT(seekdir);
    ELF_WEAK_EXPORT(mkdir);
    ELF_WEAK_EXPORT(rmdir);
    ELF_WEAK_EXPORT(chdir);
    ELF_WEAK_EXPORT(getcwd);
    ELF_WEAK_EXPORT(stat);
    ELF_WEAK_EXPORT(fstat);
    ELF_WEAK_EXPORT(lstat);
    ELF_WEAK_EXPORT(access);
    ELF_WEAK_EXPORT(chmod);
    ELF_WEAK_EXPORT(chown);
    ELF_WEAK_EXPORT(unlink);
    ELF_WEAK_EXPORT(link);
    ELF_WEAK_EXPORT(symlink);
    ELF_WEAK_EXPORT(readlink);
    ELF_WEAK_EXPORT(umask);
    ELF_WEAK_EXPORT(getpid);
    ELF_WEAK_EXPORT(getppid);
    ELF_WEAK_EXPORT(kill);
    ELF_WEAK_EXPORT(wait);
    ELF_WEAK_EXPORT(waitpid);
    ELF_WEAK_EXPORT(execv);
    ELF_WEAK_EXPORT(execve);
    ELF_WEAK_EXPORT(execvp);
    ELF_WEAK_EXPORT(execl);
    ELF_WEAK_EXPORT(execlp);
    ELF_WEAK_EXPORT(execle);
    ELF_WEAK_EXPORT(posix_spawn);
    ELF_WEAK_EXPORT(posix_spawnp);
    ELF_WEAK_EXPORT(_exit);
    ELF_WEAK_EXPORT(sleep);
    ELF_WEAK_EXPORT(usleep);
    ELF_WEAK_EXPORT(nanosleep);
    ELF_WEAK_EXPORT(alarm);
    ELF_WEAK_EXPORT(time);
    ELF_WEAK_EXPORT(clock);
    ELF_WEAK_EXPORT(gettimeofday);
    ELF_WEAK_EXPORT(localtime);
    ELF_WEAK_EXPORT(localtime_r);
    ELF_WEAK_EXPORT(gmtime);
    ELF_WEAK_EXPORT(gmtime_r);
    ELF_WEAK_EXPORT(mktime);
    ELF_WEAK_EXPORT(difftime);
    ELF_WEAK_EXPORT(strftime);
    ELF_WEAK_EXPORT(signal);
    ELF_WEAK_EXPORT(sigaction);
    ELF_WEAK_EXPORT(sigemptyset);
    ELF_WEAK_EXPORT(sigfillset);
    ELF_WEAK_EXPORT(sigaddset);
    ELF_WEAK_EXPORT(sigdelset);
    ELF_WEAK_EXPORT(sigismember);
    ELF_WEAK_EXPORT(sigprocmask);
    ELF_WEAK_EXPORT(raise);
    ELF_WEAK_EXPORT(pthread_create);
    ELF_WEAK_EXPORT(pthread_join);
    ELF_WEAK_EXPORT(pthread_detach);
    ELF_WEAK_EXPORT(pthread_self);
    ELF_WEAK_EXPORT(pthread_equal);
    ELF_WEAK_EXPORT(pthread_exit);
    ELF_WEAK_EXPORT(pthread_mutex_init);
    ELF_WEAK_EXPORT(pthread_mutex_destroy);
    ELF_WEAK_EXPORT(pthread_mutex_lock);
    ELF_WEAK_EXPORT(pthread_mutex_trylock);
    ELF_WEAK_EXPORT(pthread_mutex_unlock);
    ELF_WEAK_EXPORT(pthread_cond_init);
    ELF_WEAK_EXPORT(pthread_cond_destroy);
    ELF_WEAK_EXPORT(pthread_cond_wait);
    ELF_WEAK_EXPORT(pthread_cond_timedwait);
    ELF_WEAK_EXPORT(pthread_cond_signal);
    ELF_WEAK_EXPORT(pthread_cond_broadcast);
    ELF_WEAK_EXPORT(pthread_rwlock_init);
    ELF_WEAK_EXPORT(pthread_rwlock_destroy);
    ELF_WEAK_EXPORT(pthread_rwlock_rdlock);
    ELF_WEAK_EXPORT(pthread_rwlock_wrlock);
    ELF_WEAK_EXPORT(pthread_rwlock_unlock);
    ELF_WEAK_EXPORT(pthread_key_create);
    ELF_WEAK_EXPORT(pthread_key_delete);
    ELF_WEAK_EXPORT(pthread_getspecific);
    ELF_WEAK_EXPORT(pthread_setspecific);
    ELF_WEAK_EXPORT(pthread_once);
    ELF_WEAK_EXPORT(pthread_barrier_init);
    ELF_WEAK_EXPORT(pthread_barrier_destroy);
    ELF_WEAK_EXPORT(pthread_barrier_wait);
    ELF_WEAK_EXPORT(socket);
    ELF_WEAK_EXPORT(bind);
    ELF_WEAK_EXPORT(connect);
    ELF_WEAK_EXPORT(listen);
    ELF_WEAK_EXPORT(accept);
    ELF_WEAK_EXPORT(shutdown);
    ELF_WEAK_EXPORT(send);
    ELF_WEAK_EXPORT(recv);
    ELF_WEAK_EXPORT(sendto);
    ELF_WEAK_EXPORT(recvfrom);
    ELF_WEAK_EXPORT(getsockname);
    ELF_WEAK_EXPORT(getpeername);
    ELF_WEAK_EXPORT(setsockopt);
    ELF_WEAK_EXPORT(getsockopt);
    ELF_WEAK_EXPORT(getaddrinfo);
    ELF_WEAK_EXPORT(freeaddrinfo);
    ELF_WEAK_EXPORT(inet_addr);
    ELF_WEAK_EXPORT(inet_ntoa);
    ELF_WEAK_EXPORT(htons);
    ELF_WEAK_EXPORT(htonl);
    ELF_WEAK_EXPORT(ntohs);
    ELF_WEAK_EXPORT(ntohl);
    ELF_WEAK_EXPORT(bk_sys_uptime_ms);
    ELF_WEAK_EXPORT(bk_sys_getpid);
    ELF_WEAK_EXPORT(bk_sys_sleep_ms);
    ELF_WEAK_EXPORT(bk_gui_desktop);
    ELF_WEAK_EXPORT(bk_vision_claim);
    ELF_WEAK_EXPORT(bk_vision_initialize_engine);
    ELF_WEAK_EXPORT(bk_vision_start_shell);
    ELF_WEAK_EXPORT(bk_vision_publish_ready);
    ELF_WEAK_EXPORT(bk_vision_step);
    ELF_WEAK_EXPORT(bk_vision_release);
    ELF_WEAK_EXPORT(bk_vision_desktop);
    ELF_WEAK_EXPORT(bk_vision_setup_mode);
    ELF_WEAK_EXPORT(bk_vision_take_layout_refresh);
    ELF_WEAK_EXPORT(bk_gui_create_window);
    ELF_WEAK_EXPORT(bk_gui_set_window_content);
    ELF_WEAK_EXPORT(bk_gui_set_window_event_handler);
    ELF_WEAK_EXPORT(bk_gui_request_paint);
    ELF_WEAK_EXPORT(bk_gui_register_program);
    ELF_WEAK_EXPORT(bk_gui_unregister_program);
    ELF_WEAK_EXPORT(bk_gui_invalidate_all);
    ELF_WEAK_EXPORT(bk_input_mouse);
    ELF_WEAK_EXPORT(bk_ticks);
    ELF_WEAK_EXPORT(bk_file_open);
    ELF_WEAK_EXPORT(bk_file_close);
    ELF_WEAK_EXPORT(bk_file_read);
    ELF_WEAK_EXPORT(bk_file_write);
    ELF_WEAK_EXPORT(bk_file_seek);
    ELF_WEAK_EXPORT(bk_file_size);
    ELF_WEAK_EXPORT(bk_file_stat);
    ELF_WEAK_EXPORT(bk_file_exists);
    ELF_WEAK_EXPORT(bk_file_remove);
    ELF_WEAK_EXPORT(bk_file_transfer_check);
    ELF_WEAK_EXPORT(bk_file_rename);
    ELF_WEAK_EXPORT(bk_file_mkdir);
    ELF_WEAK_EXPORT(bk_file_rmdir);
    ELF_WEAK_EXPORT(bk_file_list);
    ELF_WEAK_EXPORT(bk_process_spawn);
    ELF_WEAK_EXPORT(bk_process_wait);
    ELF_WEAK_EXPORT(bk_process_kill);
    ELF_WEAK_EXPORT(bk_process_exit);
    ELF_WEAK_EXPORT(bk_clipboard_get_text);
    ELF_WEAK_EXPORT(bk_clipboard_set_text);
    ELF_WEAK_EXPORT(bk_gui_image_decode_png);
    ELF_WEAK_EXPORT(bk_gui_image_decode_bmp);
    ELF_WEAK_EXPORT(bk_gui_image_decode_gif);
    ELF_WEAK_EXPORT(bk_gui_image_decode_jpeg);

    #undef ELF_WEAK_EXPORT

    EXPORT(stderr);
    EXPORT(stdin);
    EXPORT(stdout);
    EXPORT(strcasecmp);
    EXPORT(strcat);
    EXPORT(strchr);
    EXPORT(strcmp);
    EXPORT(strcpy);
    EXPORT(strdup);
    EXPORT(strlen);
    EXPORT(strncasecmp);
    EXPORT(strncmp);
    EXPORT(strncpy);
    EXPORT(strrchr);
    EXPORT(strstr);
    EXPORT(system);
    EXPORT(toupper);
    EXPORT(vfprintf);
    EXPORT(vsnprintf);

    /* BlesKernOS public API */
    EXPORT(bk_sys_api_version);
    EXPORT(bk_sys_capabilities);
    EXPORT(bk_sys_log);
    EXPORT(bk_console_read);
    EXPORT(bk_console_putchar);
    EXPORT(bk_console_write);
    EXPORT(bk_sys_getpid);
    EXPORT(bk_sys_yield);
    EXPORT(bk_sys_sleep_ticks);
    EXPORT(bk_sys_sleep_ms);
    EXPORT(bk_sys_ticks);
    EXPORT(bk_sys_tick_frequency);
    EXPORT(bk_sys_uptime_ms);
    EXPORT(bk_sys_reboot);
    EXPORT(bk_sys_shutdown);
    EXPORT(bk_sys_alloc);
    EXPORT(bk_sys_alloc_zero);
    EXPORT(bk_sys_realloc);
    EXPORT(bk_sys_free);
    EXPORT(bk_sys_memory_info);
    EXPORT(bk_proc_priority);
    EXPORT(bk_proc_end_process);
    EXPORT(bk_event_count);
    EXPORT(bk_event_get);
    EXPORT(bk_event_export);
    EXPORT(bk_startup_count);
    EXPORT(bk_startup_get);
    EXPORT(bk_startup_save);
    EXPORT(bk_startup_remove);
    EXPORT(bk_service_control);
    EXPORT(bk_service_ready);
    EXPORT(bk_memory_detail);
    EXPORT(bk_vm_statistics);
    EXPORT(bk_memory_subsystems);
    EXPORT(bk_vm_swap_enable);
    EXPORT(bk_network_profile_apply);
    EXPORT(bk_network_profile_get);

    EXPORT(bk_console_printf);
    EXPORT(bk_string_length);
    EXPORT(bk_string_compare);
    EXPORT(bk_string_copy_n);
    EXPORT(bk_string_concat);
    EXPORT(bk_memory_compare);
    EXPORT(bk_proc_launch_arg_copy);
    EXPORT(bk_proc_cpu_usage);
    EXPORT(bk_proc_cpu_count);
    EXPORT(bk_proc_cpu_usage_core);
    EXPORT(bk_proc_current_cpu);
    EXPORT(bk_proc_set_affinity);
    EXPORT(bk_proc_get_affinity);
    EXPORT(bk_proc_runqueue_depth);
    EXPORT(bk_proc_scheduler_steals);
    EXPORT(bk_proc_scheduler_migrations);
    EXPORT(bk_proc_scheduler_ipis);
    EXPORT(bk_perf_enabled);
    EXPORT(bk_perf_set_enabled);
    EXPORT(bk_perf_reset);
    EXPORT(bk_perf_snapshot);
    EXPORT(bk_perf_benchmark);
    EXPORT(bk_device_block_count);
    EXPORT(bk_device_block_info);
    EXPORT(bk_device_usb_count);
    EXPORT(bk_device_usb_info);
    EXPORT(bk_device_pci_count);
    EXPORT(bk_device_pci_info);
    EXPORT(bk_device_driver_count);
    EXPORT(bk_device_driver_info);
    EXPORT(bk_device_volume_info);
    EXPORT(bk_device_defrag_begin);
    EXPORT(bk_device_defrag_step);
    EXPORT(bk_device_defrag_status);
    EXPORT(bk_device_defrag_cancel);
    EXPORT(bk_device_check_volume);
    EXPORT(bk_device_repair_volume);
    EXPORT(bk_device_partition_count);
    EXPORT(bk_device_partition_info);
    EXPORT(bk_device_mount_volume);

    EXPORT(bk_net_get_info);
    EXPORT(bk_net_dhcp);
    EXPORT(bk_net_ping);
    EXPORT(bk_net_resolve);
    EXPORT(bk_net_socket_open);
    EXPORT(bk_net_socket_connect);
    EXPORT(bk_net_socket_bind);
    EXPORT(bk_net_socket_listen);
    EXPORT(bk_net_socket_accept);
    EXPORT(bk_net_socket_send);
    EXPORT(bk_net_socket_receive);
    EXPORT(bk_net_socket_sendto);
    EXPORT(bk_net_socket_receivefrom);
    EXPORT(bk_net_socket_readable);
    EXPORT(bk_net_socket_close);
    EXPORT(bk_net_http_get);
    EXPORT(bk_net_http_exchange);
    EXPORT(bk_net_http_exchange_quiet);
    EXPORT(bk_net_tls_last_error);
    EXPORT(bk_net_socket_snapshot);
    EXPORT(network_register_socket_inspector);
    EXPORT(bk_clipboard_set_text);
    EXPORT(bk_clipboard_get_text);
    EXPORT(bk_clipboard_clear);
    EXPORT(bk_clipboard_generation);
    EXPORT(bk_data_inflate_zlib);
    EXPORT(bk_data_inflate_gzip);

    EXPORT(bk_file_open);
    EXPORT(bk_file_read);
    EXPORT(bk_file_write);
    EXPORT(bk_file_seek);
    EXPORT(bk_file_tell);
    EXPORT(bk_file_size);
    EXPORT(bk_file_close);
    EXPORT(bk_file_read_all);
    EXPORT(bk_file_write_all);
    EXPORT(bk_file_list_dir);
    EXPORT(bk_file_stat);
    EXPORT(bk_file_chdir);
    EXPORT(bk_file_getcwd);
    EXPORT(bk_file_mkdir);
    EXPORT(bk_file_remove);
    EXPORT(bk_file_transfer_check);
    EXPORT(bk_file_get_mode);
    EXPORT(bk_file_set_mode);
    EXPORT(bk_file_touch);
    EXPORT(bk_file_space_path);
    EXPORT(bk_file_sync);
    EXPORT(bk_device_attach_volume);
    EXPORT(bk_device_unmount_volume);
    EXPORT(bk_device_partition_edit);
    EXPORT(bk_file_rename);
    EXPORT(bk_setup_extract_package);
    EXPORT(bk_setup_get_progress);
    EXPORT(bk_file_space);
    EXPORT(bk_lang_get);
    EXPORT(bk_lang_translate);
    EXPORT(bk_lang_current);
    EXPORT(bk_lang_generation);
    EXPORT(bk_lang_count);
    EXPORT(bk_lang_info);
    EXPORT(bk_lang_set);
    EXPORT(bk_device_format_fat);
    EXPORT(bk_print_lpt_count);
    EXPORT(bk_print_lpt_info);
    EXPORT(bk_print_port_info);
    EXPORT(bk_print_device_id);
    EXPORT(bk_print_lpt_write);
    EXPORT(bk_print_spooler_set_ready);
    EXPORT(bk_print_spooler_is_ready);
    EXPORT(bk_sdr_count);
    EXPORT(bk_sdr_info);
    EXPORT(bk_sdr_open);
    EXPORT(bk_sdr_close);
    EXPORT(bk_sdr_set_frequency);
    EXPORT(bk_sdr_set_sample_rate);
    EXPORT(bk_sdr_set_gain);
    EXPORT(bk_sdr_read);
    EXPORT(bk_file_dialog_open);

    EXPORT(bk_gui_desktop);
    EXPORT(bk_gui_file_drag_start);
    EXPORT(bk_gui_file_drag_start_icon);
    EXPORT(bk_gui_file_drag_cancel);
    EXPORT(bk_gui_file_drop_take);
    EXPORT(bk_gui_files_sequence);
    EXPORT(bk_gui_files_changed);

    EXPORT(bk_gui_request_paint);
    EXPORT(bk_gui_register_program);
    EXPORT(bk_gui_unregister_program);
    EXPORT(bk_gui_invalidate_all);
    EXPORT(bk_vision_claim);
    EXPORT(bk_vision_initialize_engine);
    EXPORT(bk_vision_start_shell);
    EXPORT(bk_vision_publish_ready);
    EXPORT(bk_vision_step);
    EXPORT(bk_vision_release);
    EXPORT(bk_vision_desktop);
    EXPORT(bk_vision_setup_mode);
    EXPORT(bk_vision_take_layout_refresh);
    EXPORT(bk_gui_create_window);
    EXPORT(bk_gui_alert);
    EXPORT(bk_gui_error);
    EXPORT(bk_gui_network_error);
    EXPORT(bk_gui_close_window);
    EXPORT(bk_gui_focus_window);
    EXPORT(bk_gui_set_window_content);
    EXPORT(bk_gui_set_window_event_handler);
    EXPORT(bk_gui_window_set_extended_events);
    EXPORT(bk_gui_window_set_streaming);
    EXPORT(bk_gui_window_set_compact_content);
    EXPORT(bk_gui_window_present_xrgb);
    EXPORT(bk_gui_set_window_min_size);
    EXPORT(bk_gui_add_menu);
    EXPORT(bk_gui_add_menu_item);
    EXPORT(bk_gui_destroy_window);
    EXPORT(bk_gui_window_is_open);
    EXPORT(bk_gui_window_set_owner);
    EXPORT(bk_gui_window_invalidate);
    EXPORT(bk_gui_window_set_gpu_viewport);
    EXPORT(bk_gui_window_clear_gpu_viewport);
    EXPORT(bk_gui_gpu_viewport_supported);
    EXPORT(bk_gui_window_set_text_context);
    EXPORT(bk_gui_window_clear_text_context);
    EXPORT(bk_gui_window_begin_immediate_paint);
    EXPORT(bk_gui_window_end_immediate_paint);
    EXPORT(bk_gui_window_set_bounds);
    EXPORT(bk_gui_window_set_borderless);
    EXPORT(bk_gui_window_bounds);
    EXPORT(bk_gui_window_content_rect);
    EXPORT(bk_gui_surface_clear);
    EXPORT(bk_gui_surface_putpixel);
    EXPORT(bk_gui_surface_fill_rect);
    EXPORT(bk_gui_surface_draw_rect);
    EXPORT(bk_gui_surface_draw_line);
    EXPORT(bk_gui_surface_draw_polyline);
    EXPORT(bk_gui_surface_draw_text);
    EXPORT(bk_font_count);
    EXPORT(bk_font_info);
    EXPORT(bk_font_line_height);
    EXPORT(bk_font_text_width);
    EXPORT(bk_font_text_advances);
    EXPORT(bk_font_draw_text);
    EXPORT(bk_font_draw_text_styled);
    EXPORT(bk_gui_text_width_px);
    EXPORT(bk_gui_surface_draw_text_px);
    EXPORT(bk_gui_image_decode_bmp);
    EXPORT(bk_gui_image_decode_png);
    EXPORT(bk_gui_image_decode_gif);
    EXPORT(bk_gui_image_decode_jpeg);
    EXPORT(bk_gui_image_decode_svg);
    EXPORT(bk_gui_image_free);
    EXPORT(bk_gui_cursor_set_resource);
    EXPORT(bk_gui_cursor_reset);
    EXPORT(bk_gui_cursor_set_style);
    EXPORT(bk_graphics_icon_load);
    EXPORT(bk_graphics_icon_count);
    EXPORT(bk_graphics_icon_name);
    EXPORT(bk_gui_surface_draw_image);
    EXPORT(bk_gui_surface_draw_xrgb);
    EXPORT(bk_gui_surface_draw_progress);
    EXPORT(bk_gui_text_width);
    EXPORT(bk_gui_create_button);
    EXPORT(bk_gui_surface_draw_tab);
    EXPORT(bk_gui_create_slider);
    EXPORT(bk_gui_create_choice);
    EXPORT(bk_gui_slider_get_value);
    EXPORT(bk_gui_slider_set_value);
    EXPORT(bk_gui_choice_set_checked);
    EXPORT(bk_gui_choice_is_checked);

    EXPORT(bk_gui_create_textbox);
    EXPORT(bk_gui_widget_id);
    EXPORT(bk_gui_widget_set_bounds);
    EXPORT(bk_gui_widget_set_text);
    EXPORT(bk_gui_widget_set_icon);
    EXPORT(bk_gui_widget_get_text);
    EXPORT(bk_gui_widget_set_enabled);
    EXPORT(bk_gui_widget_set_visible);
    EXPORT(bk_gui_widget_set_focus);
    EXPORT(bk_gui_widget_is_focused);
    EXPORT(bk_gui_scrollbar_init_vertical);
    EXPORT(bk_gui_scrollbar_paint_vertical);
    EXPORT(bk_gui_scrollbar_handle_event_vertical);

    EXPORT(bk_gfx_info);
    EXPORT(bk_gfx_set_mode);
    EXPORT(bk_gfx_clear);
    EXPORT(bk_gfx_putpixel);
    EXPORT(bk_gfx_getpixel);
    EXPORT(bk_gfx_fill_rect);
    EXPORT(bk_gfx_present_rect);
    EXPORT(bk_gfx_flush);
    EXPORT(bk_gfx_copy_rect);
    EXPORT(bk_gfx_draw_line);
    EXPORT(bk_gfx_draw_text);
    EXPORT(bk_input_mouse);
    EXPORT(bk_input_preference_get);
    EXPORT(bk_input_preference_set);
    EXPORT(bk_input_key_event);
    EXPORT(bk_input_key_modifiers);
    EXPORT(bk_input_mouse_set_position);
    EXPORT(bk_input_mouse_set_sensitivity);
    EXPORT(bk_input_mouse_get_sensitivity);

    EXPORT(bk_sound_has_sb16);
    EXPORT(bk_sound_pcm_available);
    EXPORT(bk_sound_pcm_busy);
    EXPORT(bk_sound_pcm_name);
    EXPORT(bk_sound_play_pcm_u8);
    EXPORT(bk_sound_tone);
    EXPORT(bk_sound_stop);
    EXPORT(bk_system_sounds_enabled);
    EXPORT(bk_system_sounds_set_enabled);
    EXPORT(bk_system_sound_master_volume);
    EXPORT(bk_system_sound_set_master_volume);
    EXPORT(bk_system_sound_event_enabled);
    EXPORT(bk_system_sound_event_set_enabled);
    EXPORT(bk_system_sound_event_available);
    EXPORT(bk_system_sound_event_play);
    EXPORT(bk_system_sound_resize_begin);
    EXPORT(bk_system_sound_resize_end);
    EXPORT(bk_time_datetime);
    EXPORT(bk_datetime_runtime_preferences_get);
    EXPORT(bk_datetime_runtime_preferences_set);
    EXPORT(bk_proc_count);
    EXPORT(bk_proc_get);
    EXPORT(bk_proc_info);
    EXPORT(bk_proc_snapshot);
    EXPORT(bk_proc_request_exit);
    EXPORT(bk_proc_exit_requested);
    EXPORT(bk_proc_set_memory_hint);
    EXPORT(bk_proc_bind_window);
    EXPORT(bk_proc_launch_arg);
    EXPORT(bk_proc_spawn_thread);
    EXPORT(bk_proc_exit);
    EXPORT(bk_proc_exit_status);
    EXPORT(bk_app_launch);
    EXPORT(bk_app_run_sync);
    EXPORT(bk_shell_take_exit_request);

    /* GUI / Desktop */
    EXPORT(gui_change_resolution);
    EXPORT(gui_display_color_depth);
    EXPORT(gui_desktop_create_window);
    EXPORT(gui_desktop_focus_window);
    EXPORT(gui_desktop_invalidate_all);
    EXPORT(gui_desktop_invalidate_rect);
    EXPORT(gui_desktop_invalidate_overlay_rect);
    EXPORT(gui_desktop_raise_window);
    EXPORT(gui_desktop_remove_window);
    EXPORT(gui_desktop_register_program);
    EXPORT(gui_desktop_set_cursor_trail);
    EXPORT(gui_desktop_set_drag_outline);
    EXPORT(gui_desktop_unregister_program);
    EXPORT(gui_desktop_cursor_trail_enabled);
    EXPORT(gui_desktop_drag_outline_enabled);
    EXPORT(gui_get_last_input_tick);

    /* GUI / Drawing */
    EXPORT(gui_color_blend);
    EXPORT(gui_gfx_clear);
    EXPORT(gui_gfx_draw_line);
    EXPORT(gui_gfx_fill_rect);
    EXPORT(gui_gfx_fill_rounded_rect);
    EXPORT(gui_gfx_draw_rect);
    EXPORT(gui_gfx_putpixel);
    EXPORT(gui_gfx_point_visible);
    EXPORT(gui_gfx_invalidate_front);
    EXPORT(gui_font_draw_char);
    EXPORT(gui_font_draw_string);
    EXPORT(gui_font_draw_string_clipped);
    EXPORT(gui_font_draw_string_scaled);
    EXPORT(gui_font_text_width);
    EXPORT(gui_gif_animation_free);
    EXPORT(gui_gif_load);
    EXPORT(gui_gif_load_animation);
    EXPORT(gui_gif_load_animation_limited);
    EXPORT(gui_image_free);
    EXPORT(gui_request_paint);
    EXPORT(gui_get_desktop);

    /* GUI / Window */
    EXPORT(gui_window_add_menu);
    EXPORT(gui_window_add_menu_item);
    EXPORT(gui_window_contains);
    EXPORT(gui_window_content_rect);
    EXPORT(gui_window_content_rect_inset);
    EXPORT(gui_window_content_top);
    EXPORT(gui_window_dispatch_event);
    EXPORT(gui_window_destroy);
    EXPORT(gui_window_handle_menu_event);
    EXPORT(gui_window_context_clear);
    EXPORT(gui_window_context_add_item);
    EXPORT(gui_window_context_open);
    EXPORT(gui_window_context_close);
    EXPORT(gui_context_menu_clear);
    EXPORT(gui_context_menu_add_item);
    EXPORT(gui_context_menu_open);
    EXPORT(gui_context_menu_close);
    EXPORT(gui_context_menu_paint);
    EXPORT(gui_context_menu_handle_event);
    EXPORT(gui_window_set_content);
    EXPORT(gui_window_set_event_handler);
    EXPORT(gui_window_set_extended_events);
    EXPORT(gui_window_set_min_size);
    EXPORT(gui_window_set_borderless);
    EXPORT(gui_window_close);
    EXPORT(gui_window_minimize);
    EXPORT(gui_window_restore);
    EXPORT(gui_window_titlebar_button_at);
    EXPORT(gui_window_titlebar_contains);

    /* GUI / Widgets */
    EXPORT(gui_widget_create);
    EXPORT(gui_widget_create_button);
    EXPORT(gui_widget_create_selectable_button);
    EXPORT(gui_widget_create_listbox);
    EXPORT(gui_widget_create_dropdown);
    EXPORT(gui_widget_handle_event);
    EXPORT(gui_widget_paint);
    EXPORT(gui_widget_screen_bounds);
    EXPORT(gui_widget_set_style);
    EXPORT(gui_widget_set_selected);
    EXPORT(gui_widget_set_enabled);
    EXPORT(gui_widget_dropdown_clear);
    EXPORT(gui_widget_dropdown_add_item);
    EXPORT(gui_widget_dropdown_get_selected);
    EXPORT(gui_widget_dropdown_set_selected);
    EXPORT(gui_widget_dropdown_set_selected_by_value);
    EXPORT(gui_widget_dropdown_get_selected_label);
    EXPORT(gui_widget_dropdown_get_selected_value);
    EXPORT(gui_widget_dropdown_get_item_label);
    EXPORT(gui_widget_dropdown_get_item_value);

    /* GUI / Events / Geometry */
    EXPORT(gui_event_queue_pop);
    EXPORT(gui_event_queue_push);
    EXPORT(gui_event_queue_reset);
    EXPORT(gui_rect_contains);
    EXPORT(gui_rect_intersect);
    EXPORT(gui_scrollbar_handle_click_vertical);
    EXPORT(gui_scrollbar_handle_event_vertical);
    EXPORT(gui_scrollbar_init_vertical);
    EXPORT(gui_scrollbar_paint_vertical);
    EXPORT(gui_scrollbar_thumb_rect);

    /* Keyboard */
    EXPORT(kbd_next_event);
    EXPORT(kbd_inject_hid_usage);

    /* Kernel memory/string helpers */
    EXPORT(kmalloc);
    EXPORT(krealloc);
    EXPORT(kfree);
    EXPORT(kmemcpy);
    EXPORT(kmemcmp);
    EXPORT(kstrcmp);
    EXPORT(kmemset);
    EXPORT(mm_set_allocation_owner);
    EXPORT(mm_set_allocation_subsystem);
    EXPORT(mm_get_system_info);
    /* Boot snapshots are required by resident graphics drivers after page
     * zero has been unmapped. Export the snapshot accessors, never raw BDA
     * pointers. */
    EXPORT(mm_boot_equipment_word);
    EXPORT(mm_boot_ebda_segment);
    EXPORT(mm_boot_conventional_kb);
    EXPORT(mm_boot_vesa_read8);
    EXPORT(mm_boot_vesa_read16);
    EXPORT(mm_boot_vesa_read32);
    EXPORT(kprintf);
    EXPORT(kstrcat);
    EXPORT(kstrcpy);
    EXPORT(kstrncmp);
    EXPORT(kstrncpy);
    EXPORT(kstrlen);
    EXPORT(kzalloc);
    /* Resident ISO9660 caches share sectors across CPUs. */
    EXPORT(kspin_lock_irqsave);
    EXPORT(kspin_unlock_irqrestore);

    /* Libc integration */
    EXPORT(libc_set_exit_handler);

    /* PIT / timing */
    EXPORT(pit_get_frequency_hz);
    EXPORT(pit_get_ticks);

    /* RTC */
    EXPORT(rtc_get_time);
    EXPORT(rtc_get_date);
    EXPORT(rtc_get_datetime);

    /* Sound */
    EXPORT(sound_has_sb16);
    EXPORT(sound_pcm_available);
    EXPORT(sound_pcm_is_busy);
    EXPORT(sound_pcm_name);
    EXPORT(sound_play_pcm_u8);
    EXPORT(sound_start_tone);
    EXPORT(sound_stop);

    /* Tasks */
    EXPORT(task_bind_window);
    EXPORT(task_create);
    EXPORT(task_count);
    EXPORT(task_current_pid);
    EXPORT(task_cpu_usage);
    EXPORT(task_exit);
    EXPORT(task_exit_requested);
    EXPORT(task_get);
    EXPORT(task_launch_arg);
    EXPORT(task_preempt_disable);
    EXPORT(task_preempt_enable);
    EXPORT(task_request_exit);
    EXPORT(task_set_memory_hint);
    EXPORT(task_sleep);
    EXPORT(task_state_name);
    EXPORT(task_yield);
    EXPORT(perfmon_scope_begin);
    EXPORT(perfmon_scope_end);
    EXPORT(perfmon_scope_note);

    /* VFS */
    EXPORT(vfs_chdir);
    EXPORT(vfs_close);
    EXPORT(vfs_get_fs_info);
    EXPORT(vfs_get_mount_name);
    EXPORT(vfs_getcwd);
    EXPORT(vfs_has_cdrom);
    EXPORT(vfs_listdir);
    EXPORT(vfs_mkdir);
    EXPORT(vfs_remove);
    EXPORT(vfs_rename);
    EXPORT(vfs_get_space);
    EXPORT(vfs_get_space_path);
    EXPORT(bk_about_attach);
    EXPORT(bk_about_show);
    EXPORT(sound_play_file);
    EXPORT(startup_sound_enabled);
    EXPORT(startup_sound_set_enabled);
    EXPORT(vfs_mount);
    EXPORT(vfs_mount_default);
    EXPORT(vfs_open);
    EXPORT(vfs_read);
    EXPORT(vfs_read_all);
    EXPORT(vfs_write);
    EXPORT(vfs_write_all);

    /* Block / ISO helpers used by the external file browser */
    EXPORT(block_get);
    EXPORT(block_count);
    EXPORT(block_at);
    EXPORT(block_type_name);
    EXPORT(block_read);
    EXPORT(iso9660_mount_default);
    EXPORT(iso9660_is_mounted);
    EXPORT(iso9660_register_driver);
    EXPORT(ata_refresh_media);

    /* ABI Win32 para módulos residentes de compatibilidad. */

    /* ABI para controladores residentes .DVR. */
    EXPORT(sound_register_driver);
    EXPORT(rtc_register_driver);
    EXPORT(mouse_publish_state);
    EXPORT(mouse_register_driver);
    EXPORT(mouse_inject_relative);
    EXPORT(mouse_inject_disconnect);
    EXPORT(lpt_register_virtual_provider);
    EXPORT(lpt_register_identity_provider);
    EXPORT(lpt_unregister_virtual_provider);
    EXPORT(usb_class_register);
    EXPORT(usb_class_unregister);
    EXPORT(usb_device_count);
    EXPORT(usb_device_at);
    EXPORT(usb_interface_endpoint);
    EXPORT(usb_control_transfer);
    EXPORT(usb_endpoint_transfer);
    EXPORT(usb_clear_endpoint_halt);
    EXPORT(usb_set_interface);
    EXPORT(usb_enumerate_hub_port);
    EXPORT(usb_disconnect_hub_port);
    EXPORT(sdr_register_provider);
    EXPORT(sdr_unregister_provider);
    EXPORT(vesa_register_driver);
    EXPORT(vesa_init_from_bootinfo);
    EXPORT(vesa_attach_lfb);
    EXPORT(vesa_fill_rect_rgb);
    EXPORT(gfx_register_driver);
    EXPORT(gfx3d_register_driver);
    EXPORT(gfx3d_available);
    EXPORT(gfx3d_get_info);
    EXPORT(gfx3d_driver_name);
    EXPORT(gfx3d_capabilities);
    EXPORT(gfx3d_surface_create);
    EXPORT(gfx3d_surface_destroy);
    EXPORT(gfx3d_surface_upload);
    EXPORT(gfx3d_surface_upload_region);
    EXPORT(gfx3d_surface_upload_mip);
    EXPORT(gfx3d_surface_download);
    EXPORT(gfx3d_surface_clear);
    EXPORT(gfx3d_surface_composite);
    EXPORT(gfx3d_surface_present);
    EXPORT(gfx3d_depth_upload);
    EXPORT(gfx3d_depth_download);
    EXPORT(gfx3d_begin);
    EXPORT(gfx3d_draw_triangles);
    EXPORT(gfx3d_end);
    EXPORT(gfx3d_wait_fence);
    EXPORT(gfx3d_selftest);
    EXPORT(gfx3d_reset);
    EXPORT(r128_engine_register_2d);
    EXPORT(r128_engine_unregister_2d);
    EXPORT(r128_engine_acquire_2d);
    EXPORT(r128_engine_acquire_3d);
    EXPORT(r128_engine_release_2d);
    EXPORT(r128_engine_release_3d);
    EXPORT(r128_engine_present_vram32);
    EXPORT(r128_engine_reserved_vram_end);
    EXPORT(r128_engine_context);
    EXPORT(r128_engine_report);
    EXPORT(svga_transport_register);
    EXPORT(svga_transport_unregister);
    EXPORT(svga_transport_get);
    EXPORT(irq_install_handler);
    EXPORT(irq_uninstall_handler);
    EXPORT(driver_count);
    EXPORT(driver_at);
    EXPORT(driver_load);
    EXPORT(netdev_register);
    EXPORT(netdev_unregister);
    EXPORT(netdev_present);
    EXPORT(netdev_send);
    EXPORT(netdev_poll);
    EXPORT(netdev_receive);
    EXPORT(netdev_set_rx_handler);
    EXPORT(netdev_get_info);
    EXPORT(network_register_stack);
    EXPORT(network_unregister_stack);
    EXPORT(network_resolve);
    EXPORT(network_socket_open);
    EXPORT(network_socket_connect);
    EXPORT(network_socket_send);
    EXPORT(network_socket_receive);
    EXPORT(network_socket_close);
    EXPORT(network_http_get);
    EXPORT(network_register_tls);
    EXPORT(network_unregister_tls);

    /* Read-only hardware enumeration for Control Panel applets. */
    EXPORT(pci_device_count);
    EXPORT(pci_device_at);
    EXPORT(pci_class_name);
    EXPORT(pci_get_bar_info);
    EXPORT(pci_enable_command);
    EXPORT(pci_config_read16);
    EXPORT(pci_config_read32);
    EXPORT(pci_config_write16);
    EXPORT(pci_config_write32);

    /* VGA */
    EXPORT(vga_set_output_sink);
    /* shelllauncher.BEX is a separately loaded ELF.  Its terminal frontend
     * feeds canonical input through these helpers, so they must be visible to
     * the same ELF compatibility resolver as vga_set_output_sink(). */
    EXPORT(vga_console_input_reset);
    EXPORT(vga_console_input_push);

    /* GFX */
    EXPORT(gfx_get_info);
    EXPORT(gfx_list_display_modes);
    EXPORT(gfx_list_all_display_modes);
    EXPORT(gfx_overlay_supported);
    EXPORT(gfx_overlay_put);
    EXPORT(gfx_overlay_stop);

    /* Program helpers */
    EXPORT(deskmanager_set_background);
    EXPORT(deskmanager_get_background);
    EXPORT(deskmanager_get_wallpaper_path);
    EXPORT(deskmanager_set_wallpaper);
    EXPORT(deskmanager_get_wallpaper_mode);
    EXPORT(deskmanager_set_wallpaper_mode);
    EXPORT(elf_last_error);
    EXPORT(elf_preview_create);
    EXPORT(elf_preview_destroy);
    EXPORT(program_execute_path);
    EXPORT(program_execute_path_arg);
    EXPORT(program_draw_icon_pixels);
    EXPORT(program_is_object);
    EXPORT(program_is_win32_executable);
    EXPORT(program_launch_arg);
    EXPORT(program_load_bmp_icon_scaled);
    EXPORT(program_load_bmp_wallpaper_scaled);
    EXPORT(shell_execute_line);
    EXPORT(mouse_set_sensitivity);
    EXPORT(mouse_get_sensitivity);
    EXPORT(screensaver_get_path);
    EXPORT(screensaver_copy_path);
    EXPORT(screensaver_get_timeout_seconds);
    EXPORT(screensaver_is_enabled);
    EXPORT(screensaver_preview);
    EXPORT(screensaver_set_enabled);
    EXPORT(screensaver_set_path);
    EXPORT(screensaver_set_timeout_seconds);

    return 0;
}

static bool elf_symbol_is_shared_data(const char *name) {
    return kstrcmp(name, "errno") == 0 || kstrcmp(name, "stderr") == 0 ||
           kstrcmp(name, "stdout") == 0;
}

static uint32_t elf_user_shared_data(const char *name, uint32_t target) {
    uint32_t **published;
    uint32_t *candidate;
    uint32_t flags;
    uint32_t result;

    if (!name || !target) return target;
    if (kstrcmp(name, "errno") == 0) published = &g_user_errno_slot;
    else if (kstrcmp(name, "stdout") == 0) published = &g_user_stdout_slot;
    else if (kstrcmp(name, "stderr") == 0) published = &g_user_stderr_slot;
    else return target;

    candidate = (uint32_t *)kmalloc(sizeof(uint32_t));
    if (!candidate) return 0U;
    (void)mm_set_allocation_owner(candidate, 0U);
    *candidate = *(const uint32_t *)(uintptr_t)target;

    flags = kspin_lock_irqsave(&g_user_thunk_lock);
    if (!*published) {
        __asm__ volatile ("" : : : "memory");
        *published = candidate;
        candidate = NULL;
    }
    result = (uint32_t)(uintptr_t)*published;
    kspin_unlock_irqrestore(&g_user_thunk_lock, flags);
    if (candidate) kfree(candidate);
    return result;
}

static void elf_user_api_set_metadata(uint32_t token, const char *name,
                                      uint32_t domains, bool merge_domains) {
    char *label;
    if (token >= ELF_USER_THUNK_MAX) return;
    if (merge_domains) g_user_api_domains[token] |= domains;
    else g_user_api_domains[token] = domains;
    label = g_user_api_labels[token];
    if (label[0] || !name || !name[0]) return;
    kstrncpy(label, name, ELF_USER_API_LABEL_MAX - 1U);
    label[ELF_USER_API_LABEL_MAX - 1U] = '\0';
}

uint32_t elf_user_api_thunk(const char *name, uint32_t target) {
    uint8_t *code;
    uint8_t *candidate;
    uint32_t token;
    uint32_t domains;
    uint32_t flags;

    if (!target) return target;
    domains = kernel_domain_mask_for_api(name);
    if (elf_symbol_is_shared_data(name))
        return elf_user_shared_data(name, target);
    /* Phase 1 made kernel .text/.bss supervisor-only. The old static thunk
     * matrix therefore became non-executable from CPL3. Build a candidate in
     * the user-visible heap first, then publish it under a short lock. */
    candidate = (uint8_t *)kmalloc(ELF_USER_THUNK_SIZE);
    if (!candidate) return 0U;
    (void)mm_set_allocation_owner(candidate, 0U);
    kmemset(candidate, 0x90, ELF_USER_THUNK_SIZE);

    flags = kspin_lock_irqsave(&g_user_thunk_lock);
    for (token = 0U; token < g_user_thunk_count; token++) {
        if (g_user_targets[token] == target && g_user_thunks[token]) {
            uint32_t existing = (uint32_t)(uintptr_t)g_user_thunks[token];
            /* Aliases may classify the same target more conservatively.  Keep
             * the union so reusing a thunk can never remove synchronization. */
            elf_user_api_set_metadata(token, name, domains, true);
            kspin_unlock_irqrestore(&g_user_thunk_lock, flags);
            kfree(candidate);
            return existing;
        }
    }
    if (g_user_thunk_count >= ELF_USER_THUNK_MAX) {
        kspin_unlock_irqrestore(&g_user_thunk_lock, flags);
        kfree(candidate);
        return 0U;
    }

    token = g_user_thunk_count++;
    g_user_targets[token] = target;
    elf_user_api_set_metadata(token, name, domains, false);
    code = candidate;

    {
        code[0] = 0xB8; /* mov eax, SYS_API_CALL */
        *(uint32_t *)(void *)(code + 1) = SYS_API_CALL;
        /* ECX es caller-saved en i386. EBX no lo es: usarlo para el token
         * corrompia el estado que las aplicaciones conservan entre llamadas. */
        code[5] = 0xB9; /* mov ecx, token */
        *(uint32_t *)(void *)(code + 6) = token;
        code[10] = 0xCD; code[11] = 0x80; /* int 0x80 */

        /* Native ELF/BEX code has a flat-segment ABI. Reload the visible
         * selectors at the user-side boundary and preserve EAX. */
        code[12] = 0x50;                  /* push eax */
        code[13] = 0x66; code[14] = 0xB8; /* mov ax, GDT_USER_DATA */
        code[15] = (uint8_t)GDT_USER_DATA; code[16] = 0x00;
        code[17] = 0x8E; code[18] = 0xD8; /* mov ds, ax */
        code[19] = 0x8E; code[20] = 0xC0; /* mov es, ax */
        code[21] = 0x8E; code[22] = 0xE0; /* mov fs, ax */
        code[23] = 0x8E; code[24] = 0xE8; /* mov gs, ax */
        code[25] = 0x58;                  /* pop eax */
        code[26] = 0xC3;                  /* ret */
    }

    /* Publish last: a concurrent loader must never observe half-written
     * executable bytes. x86 preserves store order, and the compiler barrier
     * prevents C reordering across the pointer publication. */
    __asm__ volatile ("" : : : "memory");
    g_user_thunks[token] = code;
    kspin_unlock_irqrestore(&g_user_thunk_lock, flags);
    return (uint32_t)(uintptr_t)code;
}

extern gui_desktop_t *api_resolve_desktop(gui_desktop_t *desktop);
static bool elf_api_desktop_target(uint32_t target) {
    return target==(uint32_t)(uintptr_t)&gui_desktop_create_window ||
        target==(uint32_t)(uintptr_t)&gui_desktop_focus_window ||
        target==(uint32_t)(uintptr_t)&gui_desktop_invalidate_all ||
        target==(uint32_t)(uintptr_t)&gui_desktop_invalidate_rect ||
        target==(uint32_t)(uintptr_t)&gui_desktop_invalidate_overlay_rect ||
        target==(uint32_t)(uintptr_t)&gui_desktop_raise_window ||
        target==(uint32_t)(uintptr_t)&gui_desktop_remove_window ||
        target==(uint32_t)(uintptr_t)&gui_desktop_register_program ||
        target==(uint32_t)(uintptr_t)&gui_desktop_unregister_program ||
        target==(uint32_t)(uintptr_t)&gui_desktop_set_cursor_trail ||
        target==(uint32_t)(uintptr_t)&gui_desktop_set_drag_outline ||
        target==(uint32_t)(uintptr_t)&gui_desktop_cursor_trail_enabled ||
        target==(uint32_t)(uintptr_t)&gui_desktop_drag_outline_enabled;
}

uint64_t elf_user_api_dispatch(uint32_t token, const uint32_t *a,
                               bool *valid, uint32_t *callee_cleanup) {
    extern uint64_t elf_api_call_raw(uint32_t target, const uint32_t *args,
                                     uint32_t *callee_cleanup);
    const char *api_name;
    uint32_t target;
    uint32_t translated[16];
    uint64_t result;

    if (valid) *valid = false;
    if (!a || token >= g_user_thunk_count || !g_user_targets[token]) return 0;
    if (valid) *valid = true;

    api_name = g_user_api_labels[token][0]
        ? g_user_api_labels[token] : "(API sin nombre)";
    target = g_user_targets[token];
    g_last_user_api_name = api_name;
    {
        uint32_t domains = g_user_api_domains[token];
        kernel_domains_enter(domains);
        /* Old binaries still import raw GUI names through the compatibility
         * facade. Their first argument is the user desktop view, just like
         * the bk_gui_* API. Removing a window from that empty view and then
         * freeing it left a dangling link in Vision's actual desktop. */
        if(elf_api_desktop_target(target)) {
            kmemcpy(translated,a,sizeof(translated));
            translated[0]=(uint32_t)(uintptr_t)api_resolve_desktop(
                (gui_desktop_t *)(uintptr_t)a[0]);
            a=translated;
        }
        task_user_api_guard_enter(api_name, target, token);
        /* Primitive wrappers validate pointed-to memory rather than treating a
         * published function token as authority over arbitrary kernel storage. */
        if(!native_memory_dispatch(target,a,&result))
            result = elf_api_call_raw(target, a, callee_cleanup);
        else if(callee_cleanup)*callee_cleanup=0U;
        if (g_user_errno_slot) *g_user_errno_slot = (uint32_t)errno;
        task_user_api_guard_leave();
        kernel_domains_exit(domains);
    }
    return result;
}

const char *elf_last_user_api_name(void) {
    const char *name = NULL;
    if (task_user_api_guard_info(&name, NULL, NULL) && name) return name;
    return g_last_user_api_name;
}

bool elf_user_api_fault_info(const char **name_out, uint32_t *target_out,
                             uint32_t *token_out) {
    return task_user_api_guard_info(name_out, target_out, token_out);
}

void elf_user_api_fault_clear(void) {
    task_user_api_guard_clear();
}

/* Runtime COM dispatch accepts only an exact published thunk entry. The
 * diagnostic lookup below also accepts interior PCs and must not authorize
 * callbacks based on a return address within executable thunk memory. */
uint32_t elf_user_api_native_target(uint32_t entry) {
    uint32_t target=0;
    uint32_t flags=kspin_lock_irqsave(&g_user_thunk_lock);
    for(uint32_t i=0;i<g_user_thunk_count;i++) {
        if(g_user_thunks[i] && (uint32_t)(uintptr_t)g_user_thunks[i]==entry) {
            target=g_user_targets[i]; break;
        }
    }
    kspin_unlock_irqrestore(&g_user_thunk_lock,flags);
    return target;
}

bool elf_user_api_thunk_info(uint32_t address, const char **name_out,
                             uint32_t *target_out, uint32_t *token_out) {
    uint32_t flags;
    bool found = false;

    if (name_out) *name_out = NULL;
    if (target_out) *target_out = 0U;
    if (token_out) *token_out = 0U;

    flags = kspin_lock_irqsave(&g_user_thunk_lock);
    for (uint32_t token = 0U; token < g_user_thunk_count; token++) {
        uint32_t begin;
        if (!g_user_thunks[token]) continue;
        begin = (uint32_t)(uintptr_t)g_user_thunks[token];
        if (address < begin || address >= begin + ELF_USER_THUNK_SIZE)
            continue;
        if (name_out)
            *name_out = g_user_api_labels[token][0]
                ? g_user_api_labels[token] : "(API sin nombre)";
        if (target_out) *target_out = g_user_targets[token];
        if (token_out) *token_out = token;
        found = true;
        break;
    }
    kspin_unlock_irqrestore(&g_user_thunk_lock, flags);
    return found;
}


static uint32_t elf_symbol_value(const elf32_symbol_t *symbol,
                                 const char *strings,
                                 const uint32_t *section_addresses,
                                 uint16_t section_count, bool user_image) {
    if (symbol->shndx == SHN_UNDEF) {
        const char *name = strings + symbol->name;
        uint32_t target = elf_kernel_symbol(name, !user_image);
        return user_image ? elf_user_api_thunk(name, target) : target;
    }
    if (symbol->shndx == SHN_ABS) return symbol->value;
    if (symbol->shndx >= section_count ||
        section_addresses[symbol->shndx] == 0) return 0;
    return section_addresses[symbol->shndx] + symbol->value;
}

static bool elf_validate(const elf32_header_t *header, uint32_t size) {
    uint32_t section_bytes;

    if (!header || size < sizeof(*header)) {
        g_elf_error = "ELF truncado: falta el encabezado";
        return false;
    }
    if (header->ident[0] != 0x7F || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F') {
        g_elf_error = "firma ELF invalida; archivo incorrecto o corrupto";
        return false;
    }
    if (header->ident[4] != 1 || header->ident[5] != 1) {
        g_elf_error = "ELF incompatible: se requiere ELF32 little-endian";
        return false;
    }
    if (header->type != ET_REL) {
        g_elf_error = "tipo ELF incompatible: se requiere ET_REL";
        return false;
    }
    if (header->machine != EM_386) {
        g_elf_error = "arquitectura ELF incompatible: se requiere i386";
        return false;
    }
    if (header->version != EV_CURRENT) {
        g_elf_error = "version ELF invalida";
        return false;
    }
    if (header->shentsize != sizeof(elf32_section_t) || header->shnum == 0) {
        g_elf_error = "tabla de secciones ELF invalida";
        return false;
    }
    section_bytes = (uint32_t)header->shnum * header->shentsize;
    if (!elf_range_ok(header->shoff, section_bytes, size)) {
        kprintf("[ELF] archivo truncado: size=%u shoff=%u shnum=%u shentsize=%u end=%u\n",
                size, header->shoff, header->shnum, header->shentsize,
                header->shoff + section_bytes);
        g_elf_error = "ELF truncado: copia incompleta en el disco";
        return false;
    }
    return true;
}

/* Untrusted ET_REL layout: check rounding and the complete allocation sum
 * before allocating an image or publishing a pointer into it. */
static bool elf_object_layout(const elf32_section_t *sections, uint16_t count,
                              uint32_t *offsets, uint32_t *size_out,
                              uint32_t *alignment_out, uint32_t *bytes_out) {
    uint32_t size = 0U, alignment = sizeof(void *);
    for (uint16_t i = 0U; i < count; i++) {
        if (!(sections[i].flags & SHF_ALLOC)) continue;
        uint32_t a = sections[i].addralign ? sections[i].addralign : 1U;
        if ((a & (a - 1U)) || size > 0xffffffffU - (a - 1U)) return false;
        size = (size + a - 1U) & ~(a - 1U);
        offsets[i] = size;
        if (sections[i].size > 0xffffffffU - size) return false;
        size += sections[i].size;
        if (a > alignment) alignment = a;
    }
    uint64_t bytes = (uint64_t)size + alignment - 1U + sizeof(void *);
    if (!size || bytes > 0xffffffffULL || bytes > mm_heap_size()) return false;
    *size_out = size; *alignment_out = alignment; *bytes_out = (uint32_t)bytes;
    return true;
}

static bool elf_object_string_ok(const char *strings, uint32_t size,
                                 uint32_t offset) {
    if (offset >= size) return false;
    for (uint32_t i = offset; i < size; i++) if (!strings[i]) return true;
    return false;
}

static bool elf_load(const uint8_t *file, uint32_t file_size,
                     const char *entry_name, void **image_out,
                     void **entry_out, bool user_image) {
    const elf32_header_t *header = (const elf32_header_t *)file;
    const elf32_section_t *sections;
    uint32_t *addresses;
    uint8_t *image;
    uint8_t *raw_image;
    uint32_t image_size = 0;
    uint32_t image_alignment = sizeof(void *);
    uint32_t allocation_bytes, string_size = 0U;
    const elf32_section_t *symtab_section = NULL;
    const elf32_symbol_t *symbols = NULL;
    const char *strings = NULL;
    uint32_t symbol_count = 0;

    if (!entry_name || !image_out || !entry_out) {
        g_elf_error = "argumentos del cargador ELF invalidos";
        return false;
    }
    if (!elf_validate(header, file_size)) return false;
    sections = (const elf32_section_t *)(file + header->shoff);
    addresses = (uint32_t *)kzalloc(header->shnum * sizeof(uint32_t));
    if (!addresses) {
        g_elf_error = "sin memoria para secciones ELF";
        return false;
    }

    if (!elf_object_layout(sections, header->shnum, addresses, &image_size,
                           &image_alignment, &allocation_bytes)) {
        kfree(addresses);
        g_elf_error = "tamano o alineacion ELF fuera de rango";
        return false;
    }
    raw_image = (uint8_t *)kzalloc(allocation_bytes);
    if (!raw_image) {
        kfree(addresses);
        g_elf_error = "sin memoria para cargar programa";
        return false;
    }
    (void)mm_set_allocation_subsystem(raw_image,MM_SUBSYSTEM_PROCESS);
    image = (uint8_t *)elf_align(
        (uint32_t)(uintptr_t)(raw_image + sizeof(void *)), image_alignment);
    ((void **)image)[-1] = raw_image;
    for (uint16_t i = 0; i < header->shnum; i++) {
        if (!(sections[i].flags & SHF_ALLOC)) continue;
        addresses[i] += (uint32_t)(uintptr_t)image;
        if (sections[i].type == SHT_NOBITS) continue;
        if (!elf_range_ok(sections[i].offset, sections[i].size, file_size)) {
            g_elf_error = "seccion ELF fuera del archivo";
            goto fail;
        }
        kmemcpy((void *)(uintptr_t)addresses[i],
                file + sections[i].offset, sections[i].size);
    }

    for (uint16_t i = 0; i < header->shnum; i++) {
        if (sections[i].type != SHT_SYMTAB) continue;
        if (!elf_range_ok(sections[i].offset, sections[i].size, file_size) ||
            sections[i].entsize != sizeof(elf32_symbol_t) ||
            sections[i].size % sizeof(elf32_symbol_t) ||
            sections[i].link >= header->shnum) goto malformed;
        const elf32_section_t *strtab = &sections[sections[i].link];
        if (!elf_range_ok(strtab->offset, strtab->size, file_size)) goto malformed;
        symtab_section = &sections[i];
        symbols = (const elf32_symbol_t *)(file + sections[i].offset);
        strings = (const char *)(file + strtab->offset);
        string_size = strtab->size;
        symbol_count = sections[i].size / sizeof(elf32_symbol_t);
        break;
    }
    if (!symbols || !strings) {
        g_elf_error = "ELF sin tabla de simbolos";
        goto fail;
    }

    for (uint32_t i = 0; i < symbol_count; i++) {
        if (!elf_object_string_ok(strings, string_size, symbols[i].name))
            goto malformed;
        if (symbols[i].shndx == SHN_UNDEF &&
            symbols[i].name != 0 &&
            elf_symbol_value(&symbols[i], strings, addresses,
                             header->shnum, user_image) == 0 &&
            ELF32_ST_BIND(symbols[i].info) != STB_WEAK) {
            kprintf("[ELF] simbolo no resuelto: %s\n", strings + symbols[i].name);
            g_elf_error = "simbolo externo no resuelto";
            goto fail;
        }
    }

    for (uint16_t i = 0; i < header->shnum; i++) {
        const elf32_section_t *relsec = &sections[i];
        if (relsec->type != SHT_REL) continue;
        if (relsec->info >= header->shnum || relsec->link >= header->shnum ||
            &sections[relsec->link] != symtab_section ||
            !elf_range_ok(relsec->offset, relsec->size, file_size) ||
            relsec->entsize != sizeof(elf32_rel_t) ||
            relsec->size % sizeof(elf32_rel_t) ||
            addresses[relsec->info] == 0) goto malformed;

        const elf32_rel_t *rels =
            (const elf32_rel_t *)(file + relsec->offset);
        uint32_t rel_count = relsec->size / sizeof(*rels);
        for (uint32_t r = 0; r < rel_count; r++) {
            uint32_t symbol_index = rels[r].info >> 8;
            uint8_t type = (uint8_t)(rels[r].info & 0xFF);
            uint32_t target_size = sections[relsec->info].size;
            uint32_t *place;
            uint32_t symbol_value;
            if (symbol_index >= symbol_count) goto malformed;
            if (type == R_386_NONE) continue;
            if (target_size < sizeof(uint32_t) ||
                rels[r].offset > target_size - sizeof(uint32_t)) goto malformed;
            place = (uint32_t *)(uintptr_t)
                (addresses[relsec->info] + rels[r].offset);
            symbol_value = elf_symbol_value(&symbols[symbol_index], strings,
                                            addresses, header->shnum,
                                            user_image);
            if (type == R_386_NONE) {
                /* Standard no-op relocation emitted for discarded weak
                   references by ld -r. */
            } else if (type == R_386_32) {
                *place += symbol_value;
            } else if (type == R_386_PC32) {
                *place += symbol_value - (uint32_t)(uintptr_t)place;
            } else {
                g_elf_error = "tipo de relocacion ELF no soportado";
                goto fail;
            }
        }
    }

    for (uint32_t i = 0; i < symbol_count; i++) {
        if (symbols[i].shndx != SHN_UNDEF && symbols[i].name != 0 &&
            kstrcmp(strings + symbols[i].name, entry_name) == 0) {
            uint32_t value = elf_symbol_value(&symbols[i], strings, addresses,
                                              header->shnum, user_image);
            if (!value) break;
            *image_out = image;
            *entry_out = (void *)(uintptr_t)value;
            if (perfmon_enabled())
                kprintf("[PERF:ELF] entry=%s address=%x image=%x\n",
                    entry_name, value, (uint32_t)(uintptr_t)image);
            kfree(addresses);
            return true;
        }
    }
    g_elf_error = "falta el punto de entrada ELF";
    goto fail;

malformed:
    g_elf_error = "estructura ELF malformada";
fail:
    kfree(raw_image);
    kfree(addresses);
    return false;
}

/* Cargador ET_REL de bajo pico de memoria. El camino tradicional necesita el
 * archivo completo y la imagen final simultaneamente. En 8 MiB leemos sólo
 * las secciones alloc, las tablas de símbolos y pequeños lotes de relocations. */
static bool elf_read_exact(int fd, uint32_t offset, void *buffer,
                           uint32_t size) {
    int got;
    if (!buffer || !size || offset > 0x7FFFFFFFU) return false;
    if (vfs_seek(fd, (int32_t)offset, 0U) != (int32_t)offset) return false;
    got = vfs_read(fd, buffer, size);
    return got >= 0 && (uint32_t)got == size;
}

static bool elf_load_streamed(const char *path, const char *entry_name,
                              void **image_out, void **entry_out,
                              bool user_image) {
    elf32_header_t header;
    elf32_section_t *sections = NULL;
    elf32_symbol_t *symbols = NULL;
    elf32_rel_t *relocations = NULL;
    char *strings = NULL;
    uint32_t *addresses = NULL;
    uint8_t *raw_image = NULL;
    uint8_t *image;
    uint32_t image_size = 0U;
    uint32_t image_alignment = sizeof(void *);
    uint32_t allocation_bytes;
    uint32_t symbol_count = 0U;
    uint32_t string_size = 0U;
    uint16_t symtab_index = 0xFFFFU;
    uint32_t file_size;
    int fd = -1;
    bool result = false;

    if (image_out) *image_out = NULL;
    if (entry_out) *entry_out = NULL;
    if (!path || !entry_name || !image_out || !entry_out) {
        g_elf_error = "argumentos del cargador ELF streaming invalidos";
        return false;
    }
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        g_elf_error = "no se pudo abrir el programa";
        return false;
    }
    {
        int32_t measured = vfs_size(fd);
        if (measured <= 0) {
            g_elf_error = "tamano ELF invalido";
            goto done;
        }
        file_size = (uint32_t)measured;
    }
    if (!elf_read_exact(fd, 0U, &header, sizeof(header)) ||
        !elf_validate(&header, file_size)) goto done;

    sections = (elf32_section_t *)kmalloc(
        (size_t)header.shnum * sizeof(*sections));
    addresses = (uint32_t *)kzalloc(
        (size_t)header.shnum * sizeof(*addresses));
    if (!sections || !addresses ||
        !elf_read_exact(fd, header.shoff, sections,
                        (uint32_t)header.shnum * sizeof(*sections))) {
        g_elf_error = "sin memoria para tabla de secciones ELF";
        goto done;
    }

    if (!elf_object_layout(sections, header.shnum, addresses, &image_size,
                           &image_alignment, &allocation_bytes)) {
        g_elf_error = "tamano o alineacion ELF fuera de rango";
        goto done;
    }
    raw_image = (uint8_t *)kzalloc(allocation_bytes);
    if (!raw_image) {
        g_elf_error = "sin memoria para imagen ELF streaming";
        goto done;
    }
    (void)mm_set_allocation_subsystem(raw_image,MM_SUBSYSTEM_PROCESS);
    image = (uint8_t *)elf_align(
        (uint32_t)(uintptr_t)(raw_image + sizeof(void *)), image_alignment);
    ((void **)image)[-1] = raw_image;

    for (uint16_t i = 0U; i < header.shnum; i++) {
        if (!(sections[i].flags & SHF_ALLOC)) continue;
        addresses[i] += (uint32_t)(uintptr_t)image;
        if (sections[i].type == SHT_NOBITS || !sections[i].size) continue;
        if (!elf_range_ok(sections[i].offset, sections[i].size, file_size) ||
            !elf_read_exact(fd, sections[i].offset,
                            (void *)(uintptr_t)addresses[i],
                            sections[i].size)) {
            g_elf_error = "seccion ELF fuera del archivo";
            goto done;
        }
    }

    for (uint16_t i = 0U; i < header.shnum; i++) {
        if (sections[i].type != SHT_SYMTAB) continue;
        if (sections[i].entsize != sizeof(elf32_symbol_t) ||
            sections[i].size % sizeof(elf32_symbol_t) ||
            sections[i].link >= header.shnum ||
            !elf_range_ok(sections[i].offset, sections[i].size, file_size)) {
            g_elf_error = "tabla de simbolos ELF invalida";
            goto done;
        }
        elf32_section_t *strtab = &sections[sections[i].link];
        if (!elf_range_ok(strtab->offset, strtab->size, file_size)) {
            g_elf_error = "tabla de cadenas ELF invalida";
            goto done;
        }
        symbols = (elf32_symbol_t *)kmalloc(sections[i].size);
        strings = (char *)kmalloc(strtab->size + 1U);
        if (!symbols || !strings ||
            !elf_read_exact(fd, sections[i].offset, symbols,
                            sections[i].size) ||
            !elf_read_exact(fd, strtab->offset, strings, strtab->size)) {
            g_elf_error = "sin memoria para simbolos ELF";
            goto done;
        }
        strings[strtab->size] = '\0';
        string_size = strtab->size;
        symbol_count = sections[i].size / sizeof(*symbols);
        symtab_index = i;
        break;
    }
    if (!symbols || !strings) {
        g_elf_error = "ELF sin tabla de simbolos";
        goto done;
    }

    for (uint32_t i = 0U; i < symbol_count; i++) {
        if (!elf_object_string_ok(strings, string_size, symbols[i].name)) {
            g_elf_error = "nombre de simbolo ELF fuera de rango";
            goto done;
        }
        if (symbols[i].shndx == SHN_UNDEF && symbols[i].name != 0U &&
            elf_symbol_value(&symbols[i], strings, addresses,
                             header.shnum, user_image) == 0U &&
            ELF32_ST_BIND(symbols[i].info) != STB_WEAK) {
            kprintf("[ELF] simbolo no resuelto: %s\n",
                    strings + symbols[i].name);
            g_elf_error = "simbolo externo no resuelto";
            goto done;
        }
    }

    for (uint16_t i = 0U; i < header.shnum; i++) {
        elf32_section_t *relsec = &sections[i];
        if (relsec->type != SHT_REL) continue;
        if (relsec->info >= header.shnum || relsec->link != symtab_index ||
            relsec->entsize != sizeof(elf32_rel_t) ||
            relsec->size % sizeof(elf32_rel_t) ||
            !addresses[relsec->info] ||
            !elf_range_ok(relsec->offset, relsec->size, file_size)) {
            g_elf_error = "seccion de relocaciones ELF invalida";
            goto done;
        }
        /* Leer la seccion de una vez cuesta como maximo unas decenas de KiB
           en los programas actuales. En FAT32 evita volver a recorrer desde
           el primer cluster por cada bloque de 1 KiB y elimina una carrera
           con otros lectores VFS durante los yields de esa caminata. */
        relocations = (elf32_rel_t *)kmalloc(relsec->size);
        if (!relocations || !elf_read_exact(fd, relsec->offset, relocations,
                                            relsec->size)) {
            g_elf_error = "no se pudieron leer relocaciones ELF";
            goto done;
        }
        for (uint32_t r = 0U; r < relsec->size / sizeof(*relocations); r++) {
                uint32_t symbol_index = relocations[r].info >> 8U;
                uint8_t type = (uint8_t)(relocations[r].info & 0xFFU);
                uint32_t target_size = sections[relsec->info].size;
                uint32_t *place;
                uint32_t symbol_value;
                if (symbol_index >= symbol_count) {
                    g_elf_error = "simbolo de relocacion ELF fuera de rango";
                    goto done;
                }
                if (type == R_386_NONE) continue;
                if (target_size < 4U ||
                    relocations[r].offset > target_size - 4U) {
                    g_elf_error = "relocacion ELF fuera de rango";
                    goto done;
                }
                place = (uint32_t *)(uintptr_t)
                    (addresses[relsec->info] + relocations[r].offset);
                symbol_value = elf_symbol_value(&symbols[symbol_index],
                    strings, addresses, header.shnum, user_image);
                if (type == R_386_NONE) { /* no relocation */ }
                else if (type == R_386_32) *place += symbol_value;
                else if (type == R_386_PC32)
                    *place += symbol_value - (uint32_t)(uintptr_t)place;
                else {
                    g_elf_error = "tipo de relocacion ELF no soportado";
                    goto done;
                }
        }
        kfree(relocations);
        relocations = NULL;
    }

    for (uint32_t i = 0U; i < symbol_count; i++) {
        if (symbols[i].shndx != SHN_UNDEF && symbols[i].name != 0U &&
            symbols[i].name < string_size &&
            kstrcmp(strings + symbols[i].name, entry_name) == 0) {
            uint32_t value = elf_symbol_value(&symbols[i], strings, addresses,
                                              header.shnum, user_image);
            if (!value) break;
            *image_out = image;
            *entry_out = (void *)(uintptr_t)value;
            raw_image = NULL;
            result = true;
            if (perfmon_enabled())
                kprintf("[PERF:ELF] path=%s entry=%s address=%x image=%x\n",
                    path, entry_name, value, (uint32_t)(uintptr_t)image);
            kprintf("[ELF:LOW] %s cargado por secciones: imagen=%u KiB\n",
                    path, image_size / 1024U);
            break;
        }
    }
    if (!result) g_elf_error = "falta el punto de entrada ELF";

done:
    if (fd >= 0) vfs_close(fd);
    if (raw_image) kfree(raw_image);
    if (strings) kfree(strings);
    if (symbols) kfree(symbols);
    if (relocations) kfree(relocations);
    if (addresses) kfree(addresses);
    if (sections) kfree(sections);
    return result;
}

bool elf_preview_create(const char *path, gui_desktop_t *desktop,
                        void **image_out) {
    void *file = NULL;
    void *image = NULL;
    uint32_t size = 0;
    elf_program_entry_t entry = NULL;

    if (image_out) *image_out = NULL;
    if (!path || !desktop || !image_out) {
        g_elf_error = "argumentos de preview invalidos";
        return false;
    }
    if (!vfs_read_all(path, &file, &size)) {
        g_elf_error = "no se pudo leer el preview";
        return false;
    }
    if (!elf_load((const uint8_t *)file, size, "bleskernos_program_main",
                  &image, (void **)&entry, false)) {
        kfree(file);
        return false;
    }
    kfree(file);

    /* El entrypoint solo registra su gui_program sobre el desktop virtual.
       Se ejecuta sin crear una tarea para que el preview quede listo ahora. */
    entry(desktop);
    *image_out = image;
    return true;
}

void elf_preview_destroy(void *image) {
    elf_release_image(image);
}

bool elf_execute_program(const char *path, gui_desktop_t *desktop) {
    return elf_execute_program_ex(path, desktop, NULL);
}

static const char *elf_task_name(const char *path) {
    const char *name = path;

    if (!path) return "program";
    while (*path) {
        if (*path == '/' || *path == '\\') name = path + 1;
        path++;
    }
    return *name ? name : "program";
}

static bool elf_linux_vaddr_ok(const elf_linux_module_t *module,
                               uint32_t vaddr, uint32_t size) {
    return module && vaddr >= module->low &&
           vaddr + size >= vaddr && vaddr + size <= module->high;
}

static void elf_linux_copy_path(char *destination, const char *source) {
    uint32_t i=0U;
    if (!destination) return;
    if (source) while (source[i] && i+1U<ELF_LINUX_PATH_MAX) {
        destination[i]=source[i]; i++;
    }
    destination[i]='\0';
}

static bool elf_linux_string_valid(const elf_linux_module_t *module,
                                   uint32_t offset);

static const char *elf_linux_basename(const char *path) {
    const char *name=path;
    if (!path) return "";
    while (*path) { if (*path=='/' || *path=='\\') name=path+1; path++; }
    return name;
}

/* Solo el interprete didactico BlesK pide enlace en el kernel. Un ld.so
 * normal recibe PT_LOAD sin relocaciones, y resuelve DT_NEEDED/TLS por si mismo.
 * Nota ELF: propietario BLESK, tipo 0x6b01, descriptor version 1. */
static bool elf_linux_kernel_linker(const elf_linux_module_t *module) {
    for (uint16_t i=0; i<module->program_count; ++i) {
        const elf32_program_t *p=&module->programs[i];
        if (p->type!=PT_NOTE || !elf_linux_vaddr_ok(module,p->vaddr,p->filesz))
            continue;
        const uint8_t *note=(const uint8_t *)(uintptr_t)(module->access_bias+p->vaddr);
        uint32_t offset=0;
        while (p->filesz-offset>=12U) {
            uint32_t header[3];
            kmemcpy(header,note+offset,12U);
            uint32_t namesz=header[0],descsz=header[1];
            offset+=12U;
            if (namesz>p->filesz-offset || namesz>0xfffffffcU) break;
            uint32_t padded=(namesz+3U)&~3U;
            if (padded>p->filesz-offset || descsz>p->filesz-offset-padded ||
                descsz>0xfffffffcU) break;
            if (namesz==6U && descsz==4U && header[2]==0x6b01U &&
                !kmemcmp(note+offset,"BLESK",6U)) {
                uint32_t version;kmemcpy(&version,note+offset+padded,4U);
                return version==1U;
            }
            offset+=padded;
            padded=(descsz+3U)&~3U;
            if (padded>p->filesz-offset) break;
            offset+=padded;
        }
    }
    return false;
}

static bool elf_linux_parse_dynamic(elf_linux_module_t *module) {
    elf32_dynamic_t *dynamic=NULL;
    uint32_t dynamic_count=0U, hash=0U, gnu_hash=0U;
    uint32_t relent=sizeof(elf32_rel_t), syment=sizeof(elf32_symbol_t);
    if (!module || !module->programs) return false;
    for (uint16_t i=0U;i<module->program_count;i++) {
        elf32_program_t *p=&module->programs[i];
        if(p->type==PT_TLS) {
            if(p->filesz>p->memsz ||
               (p->filesz && !elf_linux_vaddr_ok(module,p->vaddr,p->filesz)) ||
               p->memsz>1024U*1024U) {
                g_elf_error="PT_TLS Linux invalido"; return false;
            }
            module->tls_vaddr=p->vaddr;
            module->tls_file_size=p->filesz;
            module->tls_memory_size=p->memsz;
            module->tls_alignment=p->align ? p->align : 1U;
            if(module->tls_alignment & (module->tls_alignment-1U)) {
                g_elf_error="alineacion PT_TLS no es potencia de dos";
                return false;
            }
        }
        if (p->type!=PT_DYNAMIC) continue;
        if(dynamic) {
            g_elf_error="multiples segmentos PT_DYNAMIC Linux"; return false;
        }
        if (!elf_linux_vaddr_ok(module,p->vaddr,p->memsz) ||
            p->memsz%sizeof(elf32_dynamic_t)) {
            g_elf_error="PT_DYNAMIC Linux fuera de rango"; return false;
        }
        dynamic=(elf32_dynamic_t *)(uintptr_t)(module->access_bias+p->vaddr);
        dynamic_count=p->memsz/sizeof(*dynamic);
    }
    if (!dynamic) return true;
    for (uint32_t i=0U;i<dynamic_count && dynamic[i].tag!=DT_NULL;i++) {
        uint32_t value=dynamic[i].value;
        switch(dynamic[i].tag) {
            case DT_NEEDED:
                if(module->needed_count>=ELF_LINUX_NEEDED_MAX) {
                    g_elf_error="demasiadas dependencias DT_NEEDED"; return false;
                }
                module->needed[module->needed_count++]=value; break;
            case DT_HASH: hash=value; break;
            case DT_GNU_HASH: gnu_hash=value; break;
            case DT_STRTAB:
                module->strings=(const char *)(uintptr_t)(module->access_bias+value);
                break;
            case DT_SYMTAB:
                module->symbols=(elf32_symbol_t *)(uintptr_t)(module->access_bias+value);
                break;
            case DT_STRSZ: module->string_size=value; break;
            case DT_SYMENT: syment=value; break;
            case DT_REL: module->rel=value; break;
            case DT_RELSZ: module->rel_size=value; break;
            case DT_RELENT: relent=value; break;
            case DT_JMPREL: module->jump_rel=value; break;
            case DT_PLTRELSZ: module->jump_rel_size=value; break;
            case DT_PLTREL:
                if(value!=DT_REL) { g_elf_error="PLT i386 no usa DT_REL"; return false; }
                break;
            case DT_SONAME: module->soname_offset=value; break;
            case DT_RPATH: module->rpath_offset=value; break;
            case DT_RUNPATH: module->runpath_offset=value; break;
            case DT_INIT: module->init=value; break;
            case DT_INIT_ARRAY: module->init_array=value; break;
            case DT_INIT_ARRAYSZ: module->init_array_size=value; break;
            case DT_RELA:
                g_elf_error="DT_RELA no pertenece a Linux i386"; return false;
            default: break;
        }
    }
    if(relent!=sizeof(elf32_rel_t)||syment!=sizeof(elf32_symbol_t)) {
        g_elf_error="entradas dinamicas i386 incompatibles"; return false;
    }
    if ((module->needed_count || module->rel_size || module->jump_rel_size) &&
        (!module->strings || !module->symbols || !module->string_size)) {
        g_elf_error="tabla dinamica Linux incompleta"; return false;
    }
    if(module->strings &&
       ((uint32_t)(uintptr_t)module->strings < module->access_bias+module->low ||
        (uint32_t)(uintptr_t)module->strings+module->string_size <
            (uint32_t)(uintptr_t)module->strings ||
        (uint32_t)(uintptr_t)module->strings+module->string_size >
            module->access_bias+module->high)) {
        g_elf_error="DT_STRTAB Linux fuera de rango"; return false;
    }
    if(hash) {
        uint32_t *table;
        if(!elf_linux_vaddr_ok(module,hash,8U)) {
            g_elf_error="DT_HASH fuera de rango"; return false;
        }
        table=(uint32_t *)(uintptr_t)(module->access_bias+hash);
        module->symbol_count=table[1];
    } else if(gnu_hash) {
        uint32_t *table,*buckets,*chains,max_symbol=0U;
        uint32_t bucket_count,symbol_offset,bloom_size;
        if(!elf_linux_vaddr_ok(module,gnu_hash,16U)) {
            g_elf_error="DT_GNU_HASH fuera de rango"; return false;
        }
        table=(uint32_t *)(uintptr_t)(module->access_bias+gnu_hash);
        bucket_count=table[0]; symbol_offset=table[1]; bloom_size=table[2];
        if(bucket_count>65536U || bloom_size>65536U ||
           !elf_linux_vaddr_ok(module,gnu_hash+16U,
               bloom_size*4U+bucket_count*4U)) {
            g_elf_error="DT_GNU_HASH invalido"; return false;
        }
        buckets=table+4U+bloom_size; chains=buckets+bucket_count;
        for(uint32_t i=0U;i<bucket_count;i++) if(buckets[i]>max_symbol)
            max_symbol=buckets[i];
        if(max_symbol) {
            uint32_t index=max_symbol;
            if(index<symbol_offset) { g_elf_error="bucket GNU invalido"; return false; }
            while(index-symbol_offset<262144U) {
                uint32_t chain_vaddr=(uint32_t)((uintptr_t)&chains[index-symbol_offset]-module->access_bias);
                if(!elf_linux_vaddr_ok(module,chain_vaddr,4U)) {
                    g_elf_error="cadena GNU hash fuera de rango"; return false;
                }
                if(chains[index-symbol_offset]&1U) { index++; break; }
                index++;
            }
            module->symbol_count=index;
        } else module->symbol_count=symbol_offset;
    }
    if(module->symbols && module->symbol_count) {
        uint32_t address=(uint32_t)(uintptr_t)module->symbols;
        uint32_t bytes=module->symbol_count*sizeof(elf32_symbol_t);
        if(address<module->access_bias+module->low || address+bytes<address ||
           address+bytes>module->access_bias+module->high) {
            g_elf_error="DT_SYMTAB Linux fuera de rango"; return false;
        }
    }
    for(uint32_t i=0U;i<module->needed_count;i++)
        if(module->needed[i]>=module->string_size) {
            g_elf_error="nombre DT_NEEDED fuera de rango"; return false;
        }
    if((module->rpath_offset &&
        !elf_linux_string_valid(module,module->rpath_offset)) ||
       (module->runpath_offset &&
        !elf_linux_string_valid(module,module->runpath_offset))) {
        g_elf_error="DT_RPATH/DT_RUNPATH fuera de rango"; return false;
    }
    return true;
}

static bool elf_linux_find_symbol(elf_linux_module_t *modules,
                                  uint32_t module_count, const char *name,
                                  uint32_t skip_module, uint32_t *value_out,
                                  const elf32_symbol_t **symbol_out,
                                  uint32_t *module_out) {
    if(!name || !*name) return false;
    for(uint32_t m=0U;m<module_count;m++) {
        elf_linux_module_t *module=&modules[m];
        if(m==skip_module || !module->symbols || !module->strings) continue;
        for(uint32_t s=1U;s<module->symbol_count;s++) {
            elf32_symbol_t *symbol=&module->symbols[s];
            if(symbol->shndx==SHN_UNDEF || symbol->name>=module->string_size)
                continue;
            if(kstrcmp(module->strings+symbol->name,name)==0) {
                if(value_out) *value_out=module->bias+symbol->value;
                if(symbol_out) *symbol_out=symbol;
                if(module_out) *module_out=m;
                return true;
            }
        }
    }
    return false;
}

static bool elf_linux_apply_rel_table(elf_linux_module_t *modules,
                                      uint32_t module_count,
                                      uint32_t module_index,
                                      uint32_t table_vaddr,
                                      uint32_t table_size) {
    elf_linux_module_t *module=&modules[module_index];
    elf32_rel_t *relocations;
    uint32_t count;
    if(!table_size) return true;
    if(table_size%sizeof(elf32_rel_t)||
       !elf_linux_vaddr_ok(module,table_vaddr,table_size)) {
        g_elf_error="tabla REL dinamica Linux fuera de rango"; return false;
    }
    relocations=(elf32_rel_t *)(uintptr_t)(module->access_bias+table_vaddr);
    count=table_size/sizeof(*relocations);
    for(uint32_t i=0U;i<count;i++) {
        uint32_t type=relocations[i].info&0xFFU;
        uint32_t symbol_index=relocations[i].info>>8;
        uint32_t *target,symbol_value=0U;
        elf32_symbol_t *symbol=NULL;
        const elf32_symbol_t *source_symbol=NULL;
        uint32_t source_module=module_index;
        if(type==R_386_NONE) continue;
        if(!elf_linux_vaddr_ok(module,relocations[i].offset,4U)) {
            g_elf_error="destino de relocacion Linux fuera de rango"; return false;
        }
        target=(uint32_t *)(uintptr_t)(module->access_bias+relocations[i].offset);
        if(type==R_386_RELATIVE) { *target+=module->bias; continue; }
        if(!module->symbols || symbol_index>=module->symbol_count) {
            g_elf_error="indice de simbolo dinamico invalido"; return false;
        }
        symbol=&module->symbols[symbol_index];
        if(symbol->name>=module->string_size) {
            g_elf_error="nombre de simbolo dinamico invalido"; return false;
        }
        bool tls_relocation=type==R_386_TLS_TPOFF || type==R_386_TLS_LE ||
            type==R_386_TLS_TPOFF32 || type==R_386_TLS_DTPMOD32 ||
            type==R_386_TLS_DTPOFF32;
        if(tls_relocation && symbol_index==0U)
            source_symbol=symbol;
        else if(symbol->shndx!=SHN_UNDEF && ELF32_ST_BIND(symbol->info)==0U)
            symbol_value=module->bias+symbol->value;
        else if(!elf_linux_find_symbol(modules,module_count,
                    module->strings+symbol->name,0xFFFFFFFFU,
                    &symbol_value,&source_symbol,&source_module)) {
            if(symbol->shndx!=SHN_UNDEF) symbol_value=module->bias+symbol->value;
            else if(ELF32_ST_BIND(symbol->info)!=STB_WEAK) {
                g_elf_error="simbolo Linux dinamico no resuelto"; return false;
            }
        }
        if(tls_relocation && symbol->shndx==SHN_UNDEF && !source_symbol) {
            g_elf_error="simbolo TLS dinamico no resuelto"; return false;
        }
        if(type==R_386_TLS_TPOFF || type==R_386_TLS_LE) {
            elf_linux_module_t *definition=&modules[source_module];
            if(!definition->tls_memory_size) {
                g_elf_error="simbolo TLS sin segmento PT_TLS"; return false;
            }
            *target+=symbol->shndx==SHN_UNDEF
                ? source_symbol->value-definition->tls_offset
                : symbol->value-definition->tls_offset;
        } else if(type==R_386_TLS_TPOFF32) {
            elf_linux_module_t *definition=&modules[source_module];
            uint32_t tls_value=symbol->shndx==SHN_UNDEF
                ? source_symbol->value : symbol->value;
            if(!definition->tls_memory_size) {
                g_elf_error="simbolo TLS32 sin PT_TLS"; return false;
            }
            *target+=definition->tls_offset-tls_value;
        } else if(type==R_386_TLS_DTPMOD32) {
            *target=modules[source_module].tls_module_id;
        } else if(type==R_386_TLS_DTPOFF32) {
            *target+=symbol->shndx==SHN_UNDEF
                ? source_symbol->value : symbol->value;
        } else if(type==R_386_32) *target+=symbol_value;
        else if(type==R_386_PC32)
            *target+=symbol_value-(module->bias+relocations[i].offset);
        else if(type==R_386_GLOB_DAT || type==R_386_JMP_SLOT) *target=symbol_value;
        else if(type==R_386_COPY) {
            if(!source_symbol || !symbol->size || source_symbol->size<symbol->size) {
                g_elf_error="R_386_COPY sin simbolo fuente valido"; return false;
            }
            elf_linux_module_t *source=&modules[source_module];
            kmemcpy(target,(const void *)(uintptr_t)
                (source->access_bias+source_symbol->value),symbol->size);
        } else { g_elf_error="tipo de relocacion i386 no soportado"; return false; }
    }
    return true;
}

static bool elf_linux_relocate_modules(elf_linux_module_t *modules,
                                       uint32_t module_count) {
    for(uint32_t i=0U;i<module_count;i++)
        if(!elf_linux_apply_rel_table(modules,module_count,i,
                modules[i].rel,modules[i].rel_size) ||
           !elf_linux_apply_rel_table(modules,module_count,i,
                modules[i].jump_rel,modules[i].jump_rel_size)) return false;
    return true;
}

static bool elf_linux_prepare_tls(elf_linux_module_t *modules,
                                  uint32_t module_count, void **raw_out,
                                  uint32_t *thread_pointer_out) {
    uint32_t total=0U,max_alignment=16U;
    uint8_t *raw,*start;
    if(raw_out) *raw_out=NULL;
    if(thread_pointer_out) *thread_pointer_out=0U;
    for(uint32_t i=0U;i<module_count;i++) {
        elf_linux_module_t *module=&modules[i];
        module->tls_module_id=i+1U;
        if(!module->tls_memory_size) continue;
        if(module->tls_alignment>max_alignment)
            max_alignment=module->tls_alignment;
        if(total>4U*1024U*1024U-module->tls_memory_size) {
            g_elf_error="TLS estatico Linux demasiado grande"; return false;
        }
        total=elf_align(total+module->tls_memory_size,module->tls_alignment);
        module->tls_offset=total;
        kprintf("[ELF:LINUX] PT_TLS modulo=%u mem=%u file=%u offset=-%u\n",
                i,module->tls_memory_size,module->tls_file_size,
                module->tls_offset);
    }
    if(!total) return true;
    total=elf_align(total,max_alignment);
    raw=(uint8_t *)kzalloc(total+max_alignment+16U+
                           (module_count+1U)*sizeof(uint32_t));
    if(!raw) { g_elf_error="sin memoria para TLS estatico Linux"; return false; }
    start=(uint8_t *)(uintptr_t)elf_align((uint32_t)(uintptr_t)raw,
                                         max_alignment);
    uint8_t *thread_pointer=start+total;
    *(uint32_t *)(void *)thread_pointer=(uint32_t)(uintptr_t)thread_pointer;
    uint32_t *dtv=(uint32_t *)(void *)(thread_pointer+8U);
    *(uint32_t *)(void *)(thread_pointer+4U)=(uint32_t)(uintptr_t)dtv;
    for(uint32_t i=0U;i<module_count;i++) {
        elf_linux_module_t *module=&modules[i];
        if(module->tls_memory_size)
            dtv[module->tls_module_id]=(uint32_t)(uintptr_t)
                (thread_pointer-module->tls_offset);
        if(!module->tls_file_size) continue;
        kmemcpy(thread_pointer-module->tls_offset,
                (const void *)(uintptr_t)(module->access_bias+module->tls_vaddr),
                module->tls_file_size);
    }
    if(raw_out) *raw_out=raw;
    if(thread_pointer_out)
        *thread_pointer_out=(uint32_t)(uintptr_t)thread_pointer;
    return true;
}

/* Primer cargador de ejecutables Linux: admite PIE i386 ET_DYN sin
 * interprete ni dependencias y copia PT_LOAD a una imagen contigua. ET_EXEC
 * y PT_INTERP se reconocen ahora de forma inequívoca, pero necesitan el
 * backend CR3 por proceso para respetar sus direcciones virtuales fijas. */
static bool elf_load_linux_pie(const uint8_t *file, uint32_t file_size,
                               void **image_out, void **entry_out,
                               uint32_t *phdr_out, uint32_t *phent_out,
                               uint32_t *phnum_out, uint32_t *bias_out,
                               uint32_t *access_bias_out,
                               uint32_t *low_out, uint32_t *high_out,
                               bool allow_interpreter, char *interpreter_out,
                               uint32_t interpreter_capacity) {
    const elf32_header_t *header = (const elf32_header_t *)file;
    const elf32_program_t *programs;
    uint32_t low = 0xFFFFFFFFU;
    uint32_t high = 0U;
    uint8_t *raw;
    uint8_t *image;
    uint32_t bias;
    uint32_t phdr_vaddr = 0U;

    if (!file || file_size < sizeof(*header) || !image_out || !entry_out ||
        !phdr_out || !phent_out || !phnum_out || !bias_out ||
        !access_bias_out ||
        !low_out || !high_out ||
        header->ident[0] != 0x7FU || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != 1U || header->ident[5] != 1U ||
        header->machine != EM_386 || header->version != EV_CURRENT) {
        g_elf_error = "ejecutable Linux ELF32/i386 invalido";
        return false;
    }
    if ((header->type != ET_DYN && header->type != ET_EXEC) ||
        header->phentsize != sizeof(elf32_program_t) || !header->phnum ||
        !elf_range_ok(header->phoff,
            (uint32_t)header->phnum * sizeof(elf32_program_t), file_size)) {
        g_elf_error = "program headers Linux ELF invalidos";
        return false;
    }
    programs = (const elf32_program_t *)(file + header->phoff);
    for (uint16_t i = 0U; i < header->phnum; i++) {
        uint32_t end;
        if (programs[i].type == PT_INTERP) {
            if (!allow_interpreter || !interpreter_out ||
                programs[i].filesz < 2U ||
                programs[i].filesz > interpreter_capacity ||
                !elf_range_ok(programs[i].offset, programs[i].filesz,
                              file_size) ||
                file[programs[i].offset + programs[i].filesz - 1U] != 0U) {
                g_elf_error = allow_interpreter
                    ? "ruta PT_INTERP Linux invalida"
                    : "el interprete Linux no puede tener PT_INTERP";
                return false;
            }
            if (interpreter_out[0]) {
                g_elf_error = "multiples segmentos PT_INTERP";
                return false;
            }
            kmemcpy(interpreter_out, file + programs[i].offset,
                    programs[i].filesz);
        }
        if (programs[i].type != PT_LOAD) continue;
        end = programs[i].vaddr + programs[i].memsz;
        if (end < programs[i].vaddr || programs[i].filesz > programs[i].memsz ||
            !elf_range_ok(programs[i].offset, programs[i].filesz, file_size)) {
            g_elf_error = "segmento PT_LOAD Linux fuera de rango";
            return false;
        }
        if (programs[i].vaddr < low) low = programs[i].vaddr;
        if (end > high) high = end;
        if (header->phoff >= programs[i].offset &&
            header->phoff + (uint32_t)header->phnum * header->phentsize <=
                programs[i].offset + programs[i].filesz)
            phdr_vaddr = programs[i].vaddr +
                (header->phoff - programs[i].offset);
    }
    low &= ~(PAGING_PAGE_SIZE - 1U);
    high = elf_align(high, PAGING_PAGE_SIZE);
    if (low == 0xFFFFFFFFU || high <= low || high - low > 32U*1024U*1024U) {
        g_elf_error = "rango virtual Linux invalido o demasiado grande";
        return false;
    }
    if(header->type==ET_EXEC &&
       (low<0x08001000U || high>0x0A000000U)) {
        g_elf_error="ET_EXEC fuera de la ventana Linux 0x08001000-0x0A000000";
        return false;
    }
    if (!phdr_vaddr) {
        g_elf_error = "tabla PHDR Linux no esta dentro de PT_LOAD";
        return false;
    }
    raw = (uint8_t *)kzalloc(high - low + PAGING_PAGE_SIZE + sizeof(void *));
    if (!raw) { g_elf_error = "sin memoria para imagen Linux"; return false; }
    image = (uint8_t *)elf_align(
        (uint32_t)(uintptr_t)(raw + sizeof(void *)), PAGING_PAGE_SIZE);
    ((void **)image)[-1] = raw;
    {
        uint32_t access_bias=(uint32_t)(uintptr_t)image-low;
        bias=header->type==ET_EXEC ? 0U : access_bias;
        *access_bias_out=access_bias;
    }
    for (uint16_t i = 0U; i < header->phnum; i++) {
        if (programs[i].type != PT_LOAD || !programs[i].filesz) continue;
        kmemcpy((void *)(uintptr_t)(*access_bias_out + programs[i].vaddr),
                file + programs[i].offset, programs[i].filesz);
    }
    if (header->entry < low || header->entry >= high) {
        kfree(raw); g_elf_error = "entrypoint Linux fuera de PT_LOAD"; return false;
    }
    *image_out = image;
    *entry_out = (void *)(uintptr_t)(bias + header->entry);
    *phdr_out = bias + phdr_vaddr;
    *phent_out = header->phentsize;
    *phnum_out = header->phnum;
    *bias_out = bias;
    *low_out = low;
    *high_out = high;
#ifdef BLESK_WINE_VERBOSE_TRACE
    kprintf("[ELF:LINUX] %s cargado base=%x entry=%x span=%u KiB\n",
            header->type==ET_EXEC ? "ET_EXEC" : "PIE",
            header->type==ET_EXEC ? low : (uint32_t)(uintptr_t)image,
            (uint32_t)(uintptr_t)*entry_out, (high-low)/1024U);
#endif
    return true;
}

static bool elf_linux_string_valid(const elf_linux_module_t *module,
                                   uint32_t offset) {
    if(!module || !module->strings || offset>=module->string_size) return false;
    for(uint32_t i=offset;i<module->string_size;i++)
        if(module->strings[i]=='\0') return true;
    return false;
}

static bool elf_linux_module_matches(const elf_linux_module_t *module,
                                     const char *name) {
    if(!module || !name) return false;
    if(kstrcmp(elf_linux_basename(module->path),elf_linux_basename(name))==0)
        return true;
    return module->soname_offset &&
           elf_linux_string_valid(module,module->soname_offset) &&
           kstrcmp(module->strings+module->soname_offset,
                   elf_linux_basename(name))==0;
}

static bool elf_linux_load_module_path(const char *path,
                                       elf_linux_module_t *module) {
    void *file=NULL;
    uint32_t size=0U,phdr=0U,phent=0U,phnum=0U,access_bias=0U;
    if(!path || !module || !vfs_read_all(path,&file,&size)) return false;
    kmemset(module,0,sizeof(*module));
    if(!elf_load_linux_pie((const uint8_t *)file,size,&module->image,
            &module->entry,&phdr,&phent,&phnum,&module->bias,&access_bias,
            &module->low,&module->high,false,NULL,0U)) {
        kfree(file); return false;
    }
    kfree(file);
    module->access_bias=access_bias;
    module->programs=(elf32_program_t *)(uintptr_t)
        (phdr+module->access_bias-module->bias);
    module->program_count=(uint16_t)phnum;
    elf_linux_copy_path(module->path,path);
    if(phent!=sizeof(elf32_program_t) || !elf_linux_parse_dynamic(module)) {
        elf_release_image(module->image); module->image=NULL; return false;
    }
    return true;
}

static bool elf_linux_join_parent(char *output,const char *parent,
                                  const char *name) {
    uint32_t slash=0U,p=0U,n=0U;
    if(!output || !parent || !name) return false;
    for(uint32_t i=0U;parent[i];i++) if(parent[i]=='/'||parent[i]=='\\')
        slash=i+1U;
    if(!slash) return false;
    while(p<slash && p+1U<ELF_LINUX_PATH_MAX) { output[p]=parent[p]; p++; }
    while(name[n] && p+1U<ELF_LINUX_PATH_MAX) output[p++]=name[n++];
    output[p]='\0';
    return name[n]=='\0';
}

static bool elf_linux_try_dependency(const char *parent,const char *name,
                                     elf_linux_module_t *module) {
    static const char *prefixes[]={
        "/lib/", "/usr/lib/", "/SYSTEM/LIBS/POSIX/", "/SYSTEM/LIBS/"
    };
    char path[ELF_LINUX_PATH_MAX];
    uint32_t p,n;
    if(!name || !*name) return false;
    for(const char *s=name;*s;s++) if(*s=='/'||*s=='\\')
        return elf_linux_load_module_path(name,module);
    if(elf_linux_join_parent(path,parent,name) &&
       elf_linux_load_module_path(path,module)) return true;
    for(uint32_t i=0U;i<sizeof(prefixes)/sizeof(prefixes[0]);i++) {
        p=0U; n=0U;
        while(prefixes[i][p] && p+1U<sizeof(path)) {
            path[p]=prefixes[i][p]; p++;
        }
        while(name[n] && p+1U<sizeof(path)) path[p++]=name[n++];
        path[p]='\0';
        if(!name[n] && elf_linux_load_module_path(path,module)) return true;
    }
    return false;
}

static bool elf_linux_try_runpath(const elf_linux_module_t *parent,
                                  const char *name,
                                  elf_linux_module_t *module) {
    const char *search;
    uint32_t origin_length=0U,position=0U,search_length;
    if(!parent || !name) return false;
    if(parent->runpath_offset)
        search=parent->strings+parent->runpath_offset;
    else if(parent->rpath_offset)
        search=parent->strings+parent->rpath_offset;
    else return false;
    search_length=parent->string_size-
        (parent->runpath_offset ? parent->runpath_offset : parent->rpath_offset);
    for(uint32_t i=0U;parent->path[i];i++)
        if(parent->path[i]=='/'||parent->path[i]=='\\') origin_length=i;
    while(position<search_length && search[position]) {
        char candidate[ELF_LINUX_PATH_MAX];
        uint32_t out=0U;
        while(position<search_length && search[position] &&
              search[position]!=':') {
            bool origin=false;
            uint32_t consumed=0U;
            if(position+6U<search_length && search[position]=='$' &&
               search[position+1U]=='O' && search[position+2U]=='R' &&
               search[position+3U]=='I' && search[position+4U]=='G' &&
               search[position+5U]=='I' && search[position+6U]=='N') {
                origin=true; consumed=7U;
            } else if(position+8U<search_length && search[position]=='$' &&
               search[position+1U]=='{' &&
               search[position+2U]=='O' && search[position+3U]=='R' &&
               search[position+4U]=='I' && search[position+5U]=='G' &&
               search[position+6U]=='I' && search[position+7U]=='N' &&
               search[position+8U]=='}') {
                origin=true; consumed=9U;
            }
            if(origin) {
                for(uint32_t i=0U;i<origin_length &&
                    out+1U<sizeof(candidate);i++)
                    candidate[out++]=parent->path[i];
                position+=consumed;
            } else {
                if(out+1U>=sizeof(candidate)) break;
                candidate[out++]=search[position++];
            }
        }
        if(!out && origin_length)
            for(uint32_t i=0U;i<origin_length && out+1U<sizeof(candidate);i++)
                candidate[out++]=parent->path[i];
        if(out && candidate[out-1U]!='/' && candidate[out-1U]!='\\' &&
           out+1U<sizeof(candidate)) candidate[out++]='/';
        for(uint32_t i=0U;name[i] && out+1U<sizeof(candidate);i++)
            candidate[out++]=name[i];
        candidate[out]='\0';
        if(out && elf_linux_load_module_path(candidate,module)) {
            kprintf("[ELF:LINUX] RUNPATH %s\n",candidate);
            return true;
        }
        if(position<search_length && search[position]==':') position++;
    }
    return false;
}

static bool elf_linux_load_dependencies(elf_linux_module_t *modules,
                                        uint32_t *module_count) {
    uint32_t cursor=0U;
    while(cursor<*module_count) {
        elf_linux_module_t *parent=&modules[cursor++];
        for(uint32_t n=0U;n<parent->needed_count;n++) {
            const char *name;
            bool found=false;
            if(!elf_linux_string_valid(parent,parent->needed[n])) {
                g_elf_error="cadena DT_NEEDED sin terminador"; return false;
            }
            name=parent->strings+parent->needed[n];
            for(uint32_t i=0U;i<*module_count;i++)
                if(elf_linux_module_matches(&modules[i],name)) { found=true; break; }
            if(found) continue;
            if(*module_count>=ELF_LINUX_MODULE_MAX) {
                g_elf_error="grafo DT_NEEDED excede limite de modulos"; return false;
            }
            if(!elf_linux_try_runpath(parent,name,&modules[*module_count]) &&
               !elf_linux_try_dependency(parent->path,name,
                                         &modules[*module_count])) {
                g_elf_error="no se encontro una biblioteca DT_NEEDED";
                return false;
            }
            kprintf("[ELF:LINUX] DT_NEEDED %s -> %s\n",name,
                    modules[*module_count].path);
            (*module_count)++;
        }
    }
    return true;
}

static void elf_linux_release_modules(elf_linux_module_t *modules,
                                      uint32_t module_count) {
    for(uint32_t i=0U;i<module_count;i++) {
        elf_release_image(modules[i].image); modules[i].image=NULL;
    }
}

static uint32_t *elf_linux_build_init_list(elf_linux_module_t *modules,
                                           uint32_t module_count,
                                           bool *ok_out) {
    uint32_t functions[64];
    uint32_t count=0U;
    if(ok_out) *ok_out=false;
    /* Reverse breadth-first load order puts every dependency before the
     * object that requested it; the main executable (slot zero) runs last. */
    for(uint32_t reverse=module_count;reverse>0U;reverse--) {
        elf_linux_module_t *module=&modules[reverse-1U];
        if(module->init) {
            if(!elf_linux_vaddr_ok(module,module->init,1U) || count>=64U) {
                g_elf_error="DT_INIT invalido o demasiados constructores";
                return NULL;
            }
            functions[count++]=module->bias+module->init;
        }
        if(module->init_array_size) {
            uint32_t *array,array_count;
            if(module->init_array_size%4U ||
               !elf_linux_vaddr_ok(module,module->init_array,
                                   module->init_array_size)) {
                g_elf_error="DT_INIT_ARRAY fuera de rango"; return NULL;
            }
            array=(uint32_t *)(uintptr_t)
                (module->access_bias+module->init_array);
            array_count=module->init_array_size/4U;
            if(array_count>64U-count) {
                g_elf_error="demasiados constructores Linux"; return NULL;
            }
            for(uint32_t i=0U;i<array_count;i++)
                if(array[i] && array[i]!=0xFFFFFFFFU) functions[count++]=array[i];
        }
    }
    if(!count) { if(ok_out) *ok_out=true; return NULL; }
    uint32_t *list=(uint32_t *)kmalloc((count+1U)*sizeof(uint32_t));
    if(!list) { g_elf_error="sin memoria para constructores Linux"; return NULL; }
    list[0]=count;
    kmemcpy(list+1U,functions,count*sizeof(uint32_t));
    if(ok_out) *ok_out=true;
    return list;
}

bool elf_execute_program_ex(const char *path, gui_desktop_t *desktop,
                            const char *launch_arg) {
    return elf_spawn_program_ex(path, desktop, launch_arg) >= 0;
}

/* ET_EXEC normally fits in one compact backing allocation.  Some perfectly
 * valid Linux executables intentionally place a few PT_LOADs very far apart
 * (Wine's i386 wine-preloader is the important example: a note at 0x08048000
 * and its code around 0x7d400000).  Representing the holes with kzalloc would
 * consume gigabytes of physical RAM, so those binaries use sparse per-PT_LOAD
 * backing and are mapped into the process at their ELF virtual addresses. */
static bool elf_linux_sparse_exec_candidate(const uint8_t *file,
                                             uint32_t file_size) {
    const elf32_header_t *header=(const elf32_header_t *)file;
    const elf32_program_t *programs;
    uint32_t low=0xffffffffU,high=0U;
    bool interp=false;
    if(!file || file_size<sizeof(*header) || header->type!=ET_EXEC ||
       header->ident[0]!=0x7fU || header->ident[1]!='E' ||
       header->ident[2]!='L' || header->ident[3]!='F' ||
       header->ident[4]!=1U || header->ident[5]!=1U ||
       header->machine!=EM_386 || header->version!=EV_CURRENT ||
       header->phentsize!=sizeof(elf32_program_t) || !header->phnum ||
       !elf_range_ok(header->phoff,
          (uint32_t)header->phnum*sizeof(elf32_program_t),file_size)) return false;
    programs=(const elf32_program_t *)(file+header->phoff);
    for(uint16_t i=0U;i<header->phnum;i++) {
        uint32_t end;
        if(programs[i].type==PT_INTERP) interp=true;
        if(programs[i].type!=PT_LOAD) continue;
        end=programs[i].vaddr+programs[i].memsz;
        if(end<programs[i].vaddr) return false;
        if(programs[i].vaddr<low) low=programs[i].vaddr;
        if(end>high) high=end;
    }
    if(interp || low==0xffffffffU || high<=low) return false;
    low&=~(PAGING_PAGE_SIZE-1U);
    high=elf_align(high,PAGING_PAGE_SIZE);
    return high-low>32U*1024U*1024U || low<0x08001000U || high>0x0A000000U;
}

static void elf_linux_sparse_release(elf_linux_sparse_load_t *loads,
                                     uint32_t count) {
    if(!loads) return;
    for(uint32_t i=0U;i<count;i++) {
        elf_release_image(loads[i].image);
        loads[i].image=NULL;
    }
}

static int elf_linux_spawn_sparse_exec(const char *path,const uint8_t *file,
                                       uint32_t file_size,
                                       const char *launch_arg,bool suspended) {
    const elf32_header_t *header=(const elf32_header_t *)file;
    const elf32_program_t *programs;
    elf_linux_sparse_load_t loads[ELF_LINUX_SPARSE_LOAD_MAX];
    uint32_t slots[ELF_LINUX_SPARSE_LOAD_MAX];
    uint32_t load_count=0U,phdr_vaddr=0U,total_pages=0U;
    bool entry_ok=false;
    int pid=-1;
    kmemset(loads,0,sizeof(loads));
    for(uint32_t i=0U;i<ELF_LINUX_SPARSE_LOAD_MAX;i++)
        slots[i]=ELF_PROCESS_IMAGE_MAX;

    if(!path || !file || file_size<sizeof(*header) || header->type!=ET_EXEC ||
       header->machine!=EM_386 || header->version!=EV_CURRENT ||
       header->phentsize!=sizeof(elf32_program_t) || !header->phnum ||
       !elf_range_ok(header->phoff,
          (uint32_t)header->phnum*sizeof(elf32_program_t),file_size)) {
        g_elf_error="ET_EXEC sparse Linux invalido"; return -1;
    }
    programs=(const elf32_program_t *)(file+header->phoff);
    for(uint16_t i=0U;i<header->phnum;i++) {
        const elf32_program_t *p=&programs[i];
        uint32_t segment_end,map_start,map_end,map_length;
        if(p->type==PT_INTERP) {
            g_elf_error="ET_EXEC sparse con PT_INTERP aun no soportado";
            return -1;
        }
        if(p->type!=PT_LOAD) continue;
        if(load_count>=ELF_LINUX_SPARSE_LOAD_MAX) {
            g_elf_error="demasiados PT_LOAD en ET_EXEC sparse"; return -1;
        }
        segment_end=p->vaddr+p->memsz;
        if(segment_end<p->vaddr || p->filesz>p->memsz ||
           !elf_range_ok(p->offset,p->filesz,file_size)) {
            g_elf_error="PT_LOAD sparse Linux fuera de rango"; return -1;
        }
        if(!p->memsz) continue;
        map_start=p->vaddr&~(PAGING_PAGE_SIZE-1U);
        map_end=elf_align(segment_end,PAGING_PAGE_SIZE);
        if(map_end<=map_start) {
            g_elf_error="PT_LOAD sparse Linux desborda"; return -1;
        }
        map_length=map_end-map_start;
        for(uint32_t j=0U;j<load_count;j++) {
            uint32_t old_end=loads[j].virtual_address+loads[j].length;
            if(map_start<old_end && loads[j].virtual_address<map_end) {
                g_elf_error="PT_LOAD sparse comparte paginas"; return -1;
            }
        }
        loads[load_count].virtual_address=map_start;
        loads[load_count].length=map_length;
        loads[load_count].writable=(p->flags&2U)!=0U;
        total_pages+=map_length/PAGING_PAGE_SIZE;
        if(header->entry>=p->vaddr && header->entry<segment_end &&
           (p->flags&1U)) entry_ok=true;
        if(header->phoff>=p->offset &&
           header->phoff+(uint32_t)header->phnum*header->phentsize<=
               p->offset+p->filesz)
            phdr_vaddr=p->vaddr+(header->phoff-p->offset);
        load_count++;
    }
    if(!load_count || !entry_ok || !phdr_vaddr) {
        g_elf_error="entry/PHDR fuera de PT_LOAD sparse"; return -1;
    }

    /* Reserve bookkeeping slots before allocating anything. */
    {
        uint32_t found=0U;
        for(uint32_t i=0U;i<ELF_PROCESS_IMAGE_MAX && found<load_count;i++)
            if(!g_process_images[i].process_id) slots[found++]=i;
        if(found<load_count) {
            g_elf_error="sin slots para PT_LOAD sparse"; return -1;
        }
    }

    for(uint16_t i=0U,load_index=0U;i<header->phnum;i++) {
        const elf32_program_t *p=&programs[i];
        uint32_t map_start,map_end,map_length,copy_offset;
        uint8_t *raw,*image;
        if(p->type!=PT_LOAD || !p->memsz) continue;
        map_start=p->vaddr&~(PAGING_PAGE_SIZE-1U);
        map_end=elf_align(p->vaddr+p->memsz,PAGING_PAGE_SIZE);
        map_length=map_end-map_start;
        raw=(uint8_t *)kzalloc(map_length+PAGING_PAGE_SIZE+sizeof(void *));
        if(!raw) {
            elf_linux_sparse_release(loads,load_index);
            g_elf_error="sin memoria para PT_LOAD sparse"; return -1;
        }
        image=(uint8_t *)elf_align(
            (uint32_t)(uintptr_t)(raw+sizeof(void *)),PAGING_PAGE_SIZE);
        ((void **)image)[-1]=raw;
        loads[load_index].image=image;
        copy_offset=p->vaddr-map_start;
        if(p->filesz) kmemcpy(image+copy_offset,file+p->offset,p->filesz);
        load_index++;
    }

    pid=task_create_user_program_suspended(elf_task_name(path),
            (task_entry_t)(uintptr_t)header->entry,NULL,launch_arg);
    if(pid<0) {
        elf_linux_sparse_release(loads,load_count);
        g_elf_error="sin slots para proceso ET_EXEC sparse"; return -1;
    }

    /* Publish ownership before mapping so every later failure can use the
     * normal process cleanup path and never leak sparse backing allocations. */
    for(uint32_t i=0U;i<load_count;i++) {
        g_process_images[slots[i]].process_id=(uint32_t)pid;
        g_process_images[slots[i]].image=loads[i].image;
        g_process_images[slots[i]].linux_address=loads[i].virtual_address;
        g_process_images[slots[i]].linux_length=loads[i].length;
        (void)mm_set_allocation_owner(((void **)loads[i].image)[-1],
                                      (uint32_t)pid);
    }

    if(!paging_process_create((uint32_t)pid)) {
        (void)task_request_exit((uint32_t)pid);
        g_elf_error="no se pudo crear CR3 para ET_EXEC sparse"; return -1;
    }
    for(uint32_t i=0U;i<load_count;i++) {
        if(!paging_process_map((uint32_t)pid,loads[i].virtual_address,
                (uint32_t)(uintptr_t)loads[i].image,loads[i].length,
                true,loads[i].writable)) {
            (void)task_request_exit((uint32_t)pid);
            g_elf_error="no se pudo mapear PT_LOAD sparse"; return -1;
        }
    }
    if(!task_set_process_linux_abi((uint32_t)pid,true) ||
       !task_configure_linux_start((uint32_t)pid,path,header->entry,
            header->entry,phdr_vaddr,header->phentsize,header->phnum,
            0U,0U) ||
       (!suspended && !task_start_suspended((uint32_t)pid))) {
        (void)task_request_exit((uint32_t)pid);
        g_elf_error="no se pudo iniciar ET_EXEC sparse Linux"; return -1;
    }
#ifdef BLESK_WINE_VERBOSE_TRACE
    kprintf("[ELF:LINUX] ET_EXEC sparse loads=%u pages=%u entry=%x phdr=%x\\n",
            load_count,total_pages,header->entry,phdr_vaddr);
#endif
    return pid;
}

static int elf_spawn_program_internal(const char *path, gui_desktop_t *desktop,
                         const char *launch_arg,bool suspended) {
    void *file = NULL;
    void *image = NULL;
    elf_program_entry_t entry = NULL;
    uint32_t size = 0;
    uint32_t image_slot = ELF_PROCESS_IMAGE_MAX;
    bool linux_image = false;
    bool linux_fixed = false;
    bool linux_kernel_link = true;
    uint32_t linux_phdr = 0U, linux_phent = 0U, linux_phnum = 0U;
    uint32_t linux_bias = 0U, interpreter_bias = 0U;
    uint32_t linux_access_bias = 0U;
    void *interpreter_image = NULL;
    void *interpreter_entry = NULL;
    uint32_t linux_low=0U,linux_high=0U;
    elf_linux_module_t linux_modules[ELF_LINUX_MODULE_MAX];
    uint32_t linux_module_count=0U;
    uint32_t linux_link_module_count=0U;
    uint32_t linux_slots[ELF_LINUX_MODULE_MAX];
    uint32_t *linux_init_list=NULL;
    void *linux_tls_raw=NULL;
    uint32_t linux_thread_pointer=0U;
    char interpreter_path[VFS_MAX_PATH];
    int pid;
#if ELF_LOAD_TIMING
    uint32_t timing_start = pit_get_ticks();
    uint32_t timing_read;
    uint32_t timing_link;
#endif

    if (!path || (!desktop && !suspended)) return -1;
    /* Apply the same RAM rule to Windows autostart and explicit launchers;
     * neither may start Wine after the boot sequence deliberately skipped it. */
    if (!wine_memory_allowed() &&
        (!strcasecmp(path,"/WINEGATE/bin/wine-launch.ELF") ||
         !strcasecmp(path,"/WINEGATE/bin/wine-launch") ||
         !strcasecmp(path,"/WINEGATE/bin/wine-tool.ELF") ||
         !strcasecmp(path,"/WINEGATE/bin/wine-tool") ||
         !strcasecmp(path,"/OPT/WINE/bin/wine") ||
         !strcasecmp(path,"/OPT/WINE/bin/wine-preloader") ||
         !strcasecmp(path,"/OPT/WINE/bin/wineserver"))) {
        g_elf_error="Wine deshabilitado: RAM fisica de 128 MiB o menos";
        return -1;
    }
    kmemset(linux_modules,0,sizeof(linux_modules));
    for(uint32_t i=0U;i<ELF_LINUX_MODULE_MAX;i++)
        linux_slots[i]=ELF_PROCESS_IMAGE_MAX;
    interpreter_path[0] = '\0';
    if (compat_mode_is_low_memory()) {
        if (!elf_load_streamed(path, "bleskernos_program_main",
                               &image, (void **)&entry, true)) return -1;
    } else {
        if (!vfs_read_all(path, &file, &size)) {
            g_elf_error = "no se pudo leer el programa";
            return -1;
        }
#if ELF_LOAD_TIMING
        timing_read = pit_get_ticks();
#endif
        if (size >= sizeof(elf32_header_t) &&
            ((const elf32_header_t *)file)->type == ET_EXEC &&
            elf_linux_sparse_exec_candidate((const uint8_t *)file,size)) {
            int sparse_pid=elf_linux_spawn_sparse_exec(path,
                (const uint8_t *)file,size,launch_arg,suspended);
            kfree(file);
            return sparse_pid;
        }
        if (size >= sizeof(elf32_header_t) &&
            (((const elf32_header_t *)file)->type == ET_DYN ||
             ((const elf32_header_t *)file)->type == ET_EXEC)) {
            linux_image = true;
            linux_fixed=((const elf32_header_t *)file)->type==ET_EXEC;
            if (!elf_load_linux_pie((const uint8_t *)file, size,
                                    &image, (void **)&entry, &linux_phdr,
                                    &linux_phent, &linux_phnum, &linux_bias,
                                    &linux_access_bias,
                                    &linux_low, &linux_high,
                                    true, interpreter_path,
                                    sizeof(interpreter_path))) {
                kfree(file);
                return -1;
            }
        } else if (!elf_load((const uint8_t *)file, size,
                             "bleskernos_program_main",
                             &image, (void **)&entry, true)) {
            kfree(file);
            return -1;
        }
        kfree(file);
        file = NULL;
        if(linux_image) {
            linux_modules[0].image=image;
            linux_modules[0].entry=(void *)entry;
            linux_modules[0].bias=linux_bias;
            linux_modules[0].access_bias=linux_access_bias;
            linux_modules[0].low=linux_low;
            linux_modules[0].high=linux_high;
            linux_modules[0].programs=(elf32_program_t *)(uintptr_t)
                (linux_phdr+linux_access_bias-linux_bias);
            linux_modules[0].program_count=(uint16_t)linux_phnum;
            elf_linux_copy_path(linux_modules[0].path,path);
            linux_module_count=1U;
        }
        if (linux_image && interpreter_path[0]) {
            if(linux_module_count>=ELF_LINUX_MODULE_MAX ||
               !elf_linux_load_module_path(interpreter_path,
                    &linux_modules[linux_module_count])) {
                elf_linux_release_modules(linux_modules,linux_module_count);
                g_elf_error = "no se pudo cargar PT_INTERP";
                return -1;
            }
            interpreter_image=linux_modules[linux_module_count].image;
            interpreter_entry=linux_modules[linux_module_count].entry;
            interpreter_bias=linux_modules[linux_module_count].bias;
            linux_module_count++;
#ifdef BLESK_WINE_VERBOSE_TRACE
            kprintf("[ELF:LINUX] PT_INTERP %s base=%x entry=%x\n",
                    interpreter_path, interpreter_bias,
                    (uint32_t)(uintptr_t)interpreter_entry);
#endif
        }
        if (linux_image && interpreter_image)
            linux_kernel_link=elf_linux_kernel_linker(&linux_modules[1]);
        if (linux_image && linux_kernel_link) {
            /* Preservar el interprete mientras se cargan sus dependencias. */
            elf_linux_module_t interpreter;
            if (interpreter_image) interpreter=linux_modules[--linux_module_count];
            if (!elf_linux_parse_dynamic(&linux_modules[0]) ||
                !elf_linux_load_dependencies(linux_modules,&linux_module_count) ||
                (interpreter_image && linux_module_count>=ELF_LINUX_MODULE_MAX)) {
                elf_linux_release_modules(linux_modules,linux_module_count);
                if (interpreter_image) elf_release_image(interpreter_image);
                return -1;
            }
            linux_link_module_count=linux_module_count;
            if (interpreter_image) linux_modules[linux_module_count++]=interpreter;
        }
#ifdef BLESK_WINE_VERBOSE_TRACE
        if (linux_image && !linux_kernel_link)
            kprintf("[ELF:LINUX] enlace delegado a PT_INTERP original\n");
#endif
        if(linux_image && linux_kernel_link &&
           !elf_linux_prepare_tls(linux_modules,linux_module_count,
                                  &linux_tls_raw,&linux_thread_pointer)) {
            elf_linux_release_modules(linux_modules,linux_module_count);
            return -1;
        }
        if(linux_image && linux_kernel_link &&
           !elf_linux_relocate_modules(linux_modules,linux_module_count)) {
            elf_linux_release_modules(linux_modules,linux_module_count);
            if(linux_tls_raw) kfree(linux_tls_raw);
            return -1;
        }
        if(linux_image && linux_kernel_link && interpreter_image) {
            bool init_ok=false;
            linux_init_list=elf_linux_build_init_list(linux_modules,
                                         linux_link_module_count,&init_ok);
            if(!init_ok) {
                elf_linux_release_modules(linux_modules,linux_module_count);
                if(linux_tls_raw) kfree(linux_tls_raw);
                return -1;
            }
        }
    }
#if ELF_LOAD_TIMING
    if (compat_mode_is_low_memory()) {
        timing_read = timing_start;
        size = 0U;
    }
    timing_link = pit_get_ticks();
#endif
    if(linux_image) {
        uint32_t found=0U;
        for(uint32_t i=0U;i<ELF_PROCESS_IMAGE_MAX && found<linux_module_count;i++)
            if(!g_process_images[i].process_id) linux_slots[found++]=i;
        if(found<linux_module_count) {
            elf_linux_release_modules(linux_modules,linux_module_count);
            if(linux_init_list) kfree(linux_init_list);
            if(linux_tls_raw) kfree(linux_tls_raw);
            g_elf_error="sin slots para registrar modulos Linux";
            return -1;
        }
        image_slot=linux_slots[0];
    } else for (uint32_t i = 0U; i < ELF_PROCESS_IMAGE_MAX; i++)
        if (!g_process_images[i].process_id) { image_slot = i; break; }
    if (image_slot == ELF_PROCESS_IMAGE_MAX) {
        elf_release_image(image);
        g_elf_error = "sin slots para registrar imagen ELF";
        return -1;
    }
    /* The real gui_desktop_t is supervisor-only. Native programs receive the
     * ABI-compatible user view returned by bk_gui_desktop(); API entry points
     * translate that handle back to the real desktop. */
    if (suspended && !linux_image) {
        elf_release_image(image);g_elf_error="exec requiere ELF Linux";return -1;
    }
    pid = linux_image
        ? task_create_user_program_suspended(elf_task_name(path),
              (task_entry_t)(interpreter_entry ? interpreter_entry :
                             (void *)entry), NULL, launch_arg)
        : task_create_user_program(elf_task_name(path),
              (task_entry_t)entry, bk_gui_desktop(), launch_arg);
    if (pid < 0) {
        if(linux_image) elf_linux_release_modules(linux_modules,
                                                  linux_module_count);
        else elf_release_image(image);
        if(linux_init_list) kfree(linux_init_list);
        if(linux_tls_raw) kfree(linux_tls_raw);
        g_elf_error = "sin slots para crear proceso";
        return -1;
    }
    if(linux_image) for(uint32_t i=0U;i<linux_module_count;i++) {
        g_process_images[linux_slots[i]].process_id=(uint32_t)pid;
        g_process_images[linux_slots[i]].image=linux_modules[i].image;
        g_process_images[linux_slots[i]].linux_address=linux_modules[i].bias+linux_modules[i].low;
        g_process_images[linux_slots[i]].linux_length=linux_modules[i].high-linux_modules[i].low;
        (void)mm_set_allocation_owner(((void **)linux_modules[i].image)[-1],
                                      (uint32_t)pid);
    } else {
        g_process_images[image_slot].process_id = (uint32_t)pid;
        g_process_images[image_slot].image = image;
        (void)mm_set_allocation_owner(((void **)image)[-1], (uint32_t)pid);
    }
    if (linux_image) {
        if(linux_init_list)
            (void)mm_set_allocation_owner(linux_init_list,(uint32_t)pid);
        if(linux_tls_raw)
            (void)mm_set_allocation_owner(linux_tls_raw,(uint32_t)pid);
        if (linux_fixed &&
            (!paging_process_create((uint32_t)pid) ||
             !paging_process_map((uint32_t)pid,linux_low,
                 (uint32_t)(uintptr_t)image,linux_high-linux_low,
                 true,true))) {
            (void)task_request_exit((uint32_t)pid);
            g_elf_error="no se pudo crear CR3 para ET_EXEC";
            return -1;
        }
        if(linux_fixed) {
            for(uint32_t page=linux_low;page<linux_high;
                page+=PAGING_PAGE_SIZE) {
                bool writable=false;
                for(uint32_t p=0U;p<linux_modules[0].program_count;p++) {
                    elf32_program_t *segment=&linux_modules[0].programs[p];
                    uint32_t segment_end=segment->vaddr+segment->memsz;
                    if(segment->type==PT_LOAD && page<segment_end &&
                       page+PAGING_PAGE_SIZE>segment->vaddr &&
                       (segment->flags&2U)) writable=true;
                }
                if(!paging_process_protect((uint32_t)pid,page,
                                           PAGING_PAGE_SIZE,writable)) {
                    (void)task_request_exit((uint32_t)pid);
                    g_elf_error="no se pudieron aplicar permisos ET_EXEC";
                    return -1;
                }
            }
        }
        if (!task_set_process_linux_abi((uint32_t)pid, true) ||
            (linux_thread_pointer &&
             !task_set_user_fs_base((uint32_t)pid,linux_thread_pointer)) ||
            !task_configure_linux_start((uint32_t)pid, path,
                (uint32_t)(uintptr_t)(interpreter_entry ? interpreter_entry :
                                      (void *)entry),
                (uint32_t)(uintptr_t)entry, linux_phdr,
                linux_phent, linux_phnum,
                interpreter_image ? interpreter_bias : 0U,
                (uint32_t)(uintptr_t)linux_init_list) ||
            (!suspended && !task_start_suspended((uint32_t)pid))) {
            (void)task_request_exit((uint32_t)pid);
            g_elf_error = "no se pudo iniciar personalidad Linux";
            return -1;
        }
    }
#if ELF_LOAD_TIMING
    kprintf("[ELF:PERF] %s bytes=%u read=%u link=%u create=%u ticks\n",
            path, size, timing_read - timing_start,
            timing_link - timing_read, pit_get_ticks() - timing_link);
#endif
    return pid;
}

int elf_spawn_program_ex(const char *path, gui_desktop_t *desktop,const char *launch_arg) {
    return elf_spawn_program_internal(path,desktop,launch_arg,false);
}

int elf_spawn_vision_service(const char *path, gui_desktop_t *desktop,
                             const char *launch_arg) {
    bool previous = g_loading_vision_service;
    int pid;

    g_loading_vision_service = true;
    pid = elf_spawn_program_internal(path, desktop, launch_arg, false);
    g_loading_vision_service = previous;
    return pid;
}

int elf_spawn_linux_suspended(const char *path) {
    return elf_spawn_program_internal(path,NULL,NULL,true);
}

int elf_spawn_linux_suspended_ex(const char *path,const char *launch_arg) {
    return elf_spawn_program_internal(path,NULL,launch_arg,true);
}

void elf_swap_process_ids(uint32_t first,uint32_t second) {
    for(uint32_t i=0;i<ELF_PROCESS_IMAGE_MAX;i++) {
        if(g_process_images[i].process_id==first)g_process_images[i].process_id=second;
        else if(g_process_images[i].process_id==second)g_process_images[i].process_id=first;
    }
}

bool elf_load_resident(const char *path, const char *entry_symbol,
                       void **image_out, void **entry_out) {
    void *file = NULL;
    uint32_t size = 0;

    if (image_out) *image_out = NULL;
    if (entry_out) *entry_out = NULL;
    if (!path || !entry_symbol || !image_out || !entry_out) {
        g_elf_error = "argumentos de modulo invalidos";
        return false;
    }
    if (!vfs_read_all(path, &file, &size)) {
        g_elf_error = "no se pudo leer el modulo";
        return false;
    }
    bool loaded = elf_load((const uint8_t *)file, size, entry_symbol,
                           image_out, entry_out, false);
    kfree(file);
    return loaded;
}

void elf_release_image(void *image) {
    if (image) kfree(((void **)image)[-1]);
}

bool elf_linux_clone_images(uint32_t parent,uint32_t child) {
    for (uint32_t i=0;i<ELF_PROCESS_IMAGE_MAX;i++) {
        elf_process_image_t *src=&g_process_images[i],*dst=NULL;
        if (src->process_id!=parent) continue;
        if (!src->linux_length) return false;
        for (uint32_t j=0;j<ELF_PROCESS_IMAGE_MAX;j++)
            if (!g_process_images[j].process_id) {dst=&g_process_images[j];break;}
        if (!dst) return false;
        void *raw=kzalloc(src->linux_length+4096+sizeof(void *));
        if (!raw) return false;
        void *image=(void *)elf_align((uint32_t)raw+sizeof(void *),4096);
        ((void **)image)[-1]=raw;
        if (!paging_process_copy(parent,child,src->linux_address,(uint32_t)image,src->linux_length)) {
            kfree(raw);return false;
        }
        void *pin=src->linux_address_pin?src->linux_address_pin:((void **)src->image)[-1];
        if(!mm_pin_address_allocation(pin)) {kfree(raw);return false;}
        *dst=*src;dst->process_id=child;dst->image=image;dst->linux_address_pin=pin;
        mm_set_allocation_owner(raw,child);
    }
    return true;
}

void elf_process_cleanup(uint32_t process_id) {
    if (!process_id) return;
    for (uint32_t i = 0U; i < ELF_PROCESS_IMAGE_MAX; i++) {
        if (g_process_images[i].process_id != process_id) continue;
        elf_release_image(g_process_images[i].image);
        if(g_process_images[i].linux_address_pin)
            mm_unpin_address_allocation(g_process_images[i].linux_address_pin);
        g_process_images[i].linux_address_pin=NULL;
        g_process_images[i].process_id = 0U;
        g_process_images[i].image = NULL;
        g_process_images[i].linux_address=0U;
        g_process_images[i].linux_length=0U;
    }
}

bool elf_process_address(uint32_t process_id, uint32_t address,
                         uint32_t *base_out, uint32_t *offset_out) {
    if (!process_id) return false;
    for (uint32_t i=0U;i<ELF_PROCESS_IMAGE_MAX;i++) {
        uint32_t base;
        if (g_process_images[i].process_id != process_id ||
            !g_process_images[i].image) continue;
        base=(uint32_t)(uintptr_t)g_process_images[i].image;
        if(address<base) continue;
        if(base_out)*base_out=base;
        if(offset_out)*offset_out=address-base;
        return true;
    }
    return false;
}

const char *elf_last_error(void) {
    return g_elf_error;
}
