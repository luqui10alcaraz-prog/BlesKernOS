#include "include/elf_loader.h"
#include "include/pe_loader.h"
#include "include/memory.h"
#include "include/vfs.h"
#include "include/about_dialog.h"
#include "include/startup_sound.h"
#include "include/task.h"
#include "include/pit.h"
#include "include/keyboard.h"
#include "include/block.h"
#include "include/pci.h"
#include "include/iso9660.h"
#include "include/shell.h"
#include "include/sound.h"
#include "include/vga.h"
#include "include/datetime_prefs.h"
#include "../gui/gui.h"
#include "../gui/image.h"
#include "../programs/programs.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "strings.h"
#include "ctype.h"
#include "math.h"
#include "errno.h"
#include "sys/stat.h"
#include "include/rtc.h"
#include "include/api.h"
#include "include/ata.h"
#include "include/driver.h"
#include "include/pic.h"
#include "include/mouse.h"
#include "include/vesa.h"
#include "include/syscall.h"

/* api_compat conserva nombres historicos para codigo interno. El cargador debe
 * exportar los simbolos reales de la ABI publica, no expandirlos a drivers. */
#undef bk_proc_cpu_usage
#undef bk_device_block_count
#undef bk_device_pci_count
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
extern bool bk_device_block_info(uint32_t index, void *info);
extern uint32_t bk_device_pci_count(void);
extern bool bk_device_pci_info(uint32_t index, void *info);
extern bool bk_device_volume_info(void *info);
extern bool bk_device_check_volume(void *report);
extern bool bk_device_repair_volume(void *repair, void *after);
extern uint32_t bk_device_partition_count(void);
extern bool bk_device_partition_info(uint32_t index, void *info);
extern bool bk_device_mount_volume(const char *device_name);

#define EI_NIDENT 16
#define ET_REL 1
#define EM_386 3
#define EV_CURRENT 1
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_NOBITS 8
#define SHT_REL 9
#define SHF_ALLOC 0x2
#define SHN_UNDEF 0
#define SHN_ABS 0xFFF1
#define R_386_32 1
#define R_386_PC32 2
#define ELF_USER_THUNK_MAX 1024U
#define ELF_USER_THUNK_SIZE 16U
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

typedef void (*elf_program_entry_t)(gui_desktop_t *desktop);

static const char *g_elf_error = "sin error";
static void *g_loaded_image;
static elf_program_entry_t g_loaded_entry;
static char g_loaded_path[VFS_MAX_PATH];
static uint8_t g_user_thunks[ELF_USER_THUNK_MAX][ELF_USER_THUNK_SIZE]
    __attribute__((aligned(16)));
static uint32_t g_user_targets[ELF_USER_THUNK_MAX];
static uint32_t g_user_thunk_count;

bool elf_preview_create(const char *path, gui_desktop_t *desktop,
                        void **image_out);
void elf_preview_destroy(void *image);

extern int __divdi3(void);

static bool elf_range_ok(uint32_t offset, uint32_t length, uint32_t total) {
    return offset <= total && length <= total - offset;
}

static uint32_t elf_align(uint32_t value, uint32_t alignment) {
    if (alignment <= 1) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

#define EXPORT(symbol) \
    if (kstrcmp(name, #symbol) == 0) return (uint32_t)(uintptr_t)&symbol

static uint32_t elf_kernel_symbol(const char *name) {
    /* GCC / runtime */
    EXPORT(__divdi3);

    /* libc / stdio / stdlib / string */
    EXPORT(abs);
    EXPORT(atof);
    EXPORT(atoi);
    EXPORT(calloc);
    EXPORT(errno);
    EXPORT(exit);
    EXPORT(fabs);
    EXPORT(cos);
    EXPORT(sin);
    EXPORT(sqrt);
    EXPORT(floor);
    EXPORT(pow);
    EXPORT(fclose);
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
    EXPORT(stderr);
    EXPORT(stdout);
    EXPORT(strcasecmp);
    EXPORT(strchr);
    EXPORT(strcmp);
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
    EXPORT(bk_console_printf);
    EXPORT(bk_string_length);
    EXPORT(bk_string_compare);
    EXPORT(bk_string_copy_n);
    EXPORT(bk_string_concat);
    EXPORT(bk_memory_compare);
    EXPORT(bk_proc_launch_arg_copy);
    EXPORT(bk_proc_cpu_usage);
    EXPORT(bk_device_block_count);
    EXPORT(bk_device_block_info);
    EXPORT(bk_device_pci_count);
    EXPORT(bk_device_pci_info);
    EXPORT(bk_device_volume_info);
    EXPORT(bk_device_check_volume);
    EXPORT(bk_device_repair_volume);
    EXPORT(bk_device_partition_count);
    EXPORT(bk_device_partition_info);
    EXPORT(bk_device_mount_volume);

    EXPORT(bk_file_open);
    EXPORT(bk_file_read);
    EXPORT(bk_file_write);
    EXPORT(bk_file_close);
    EXPORT(bk_file_read_all);
    EXPORT(bk_file_write_all);
    EXPORT(bk_file_list_dir);
    EXPORT(bk_file_chdir);
    EXPORT(bk_file_getcwd);
    EXPORT(bk_file_mkdir);
    EXPORT(bk_file_remove);
    EXPORT(bk_file_rename);
    EXPORT(bk_file_space);
    EXPORT(bk_device_format_fat);
    EXPORT(bk_file_dialog_open);

    EXPORT(bk_gui_desktop);
    EXPORT(bk_gui_request_paint);
    EXPORT(bk_gui_create_window);
    EXPORT(bk_gui_close_window);
    EXPORT(bk_gui_focus_window);
    EXPORT(bk_gui_set_window_content);
    EXPORT(bk_gui_set_window_event_handler);
    EXPORT(bk_gui_set_window_min_size);
    EXPORT(bk_gui_add_menu);
    EXPORT(bk_gui_add_menu_item);
    EXPORT(bk_gui_destroy_window);
    EXPORT(bk_gui_window_is_open);
    EXPORT(bk_gui_window_set_owner);
    EXPORT(bk_gui_window_invalidate);
    EXPORT(bk_gui_window_bounds);
    EXPORT(bk_gui_window_content_rect);
    EXPORT(bk_gui_surface_clear);
    EXPORT(bk_gui_surface_putpixel);
    EXPORT(bk_gui_surface_fill_rect);
    EXPORT(bk_gui_surface_draw_rect);
    EXPORT(bk_gui_surface_draw_line);
    EXPORT(bk_gui_surface_draw_text);
    EXPORT(bk_gui_text_width);

    EXPORT(bk_gfx_info);
    EXPORT(bk_gfx_set_mode);
    EXPORT(bk_gfx_clear);
    EXPORT(bk_gfx_putpixel);
    EXPORT(bk_gfx_getpixel);
    EXPORT(bk_gfx_fill_rect);
    EXPORT(bk_gfx_draw_line);
    EXPORT(bk_gfx_draw_text);
    EXPORT(bk_input_mouse);
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
    EXPORT(bk_time_datetime);
    EXPORT(bk_datetime_runtime_preferences_get);
    EXPORT(bk_datetime_runtime_preferences_set);
    EXPORT(bk_proc_count);
    EXPORT(bk_proc_get);
    EXPORT(bk_proc_info);
    EXPORT(bk_proc_request_exit);
    EXPORT(bk_proc_exit_requested);
    EXPORT(bk_proc_set_memory_hint);
    EXPORT(bk_proc_bind_window);
    EXPORT(bk_proc_launch_arg);
    EXPORT(bk_proc_spawn_thread);
    EXPORT(bk_proc_exit);
    EXPORT(bk_app_launch);
    EXPORT(bk_shell_take_exit_request);

    /* GUI / Desktop */
    EXPORT(gui_change_resolution);
    EXPORT(gui_desktop_create_window);
    EXPORT(gui_desktop_focus_window);
    EXPORT(gui_desktop_invalidate_all);
    EXPORT(gui_desktop_invalidate_rect);
    EXPORT(gui_desktop_raise_window);
    EXPORT(gui_desktop_remove_window);
    EXPORT(gui_desktop_register_program);
    EXPORT(gui_desktop_set_cursor_trail);
    EXPORT(gui_desktop_unregister_program);
    EXPORT(gui_desktop_cursor_trail_enabled);
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

    /* Kernel memory/string helpers */
    EXPORT(kmalloc);
    EXPORT(krealloc);
    EXPORT(kfree);
    EXPORT(kmemcpy);
    EXPORT(kmemcmp);
    EXPORT(kstrcmp);
    EXPORT(kmemset);
    EXPORT(mm_get_system_info);
    EXPORT(kprintf);
    EXPORT(kstrcat);
    EXPORT(kstrcpy);
    EXPORT(kstrncmp);
    EXPORT(kstrncpy);
    EXPORT(kstrlen);
    EXPORT(kzalloc);

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
    EXPORT(iso9660_register_driver);
    EXPORT(ata_refresh_media);

    /* ABI para controladores residentes .DVR. */
    EXPORT(sound_register_driver);
    EXPORT(rtc_register_driver);
    EXPORT(mouse_register_driver);
    EXPORT(vesa_register_driver);
    EXPORT(irq_install_handler);
    EXPORT(irq_uninstall_handler);
    EXPORT(driver_count);
    EXPORT(driver_at);
    EXPORT(driver_load);

    /* Read-only hardware enumeration for Control Panel applets. */
    EXPORT(pci_device_count);
    EXPORT(pci_device_at);
    EXPORT(pci_class_name);
    EXPORT(pci_enable_command);
    EXPORT(pci_config_read16);
    EXPORT(pci_config_read32);
    EXPORT(pci_config_write16);
    EXPORT(pci_config_write32);

    /* VGA */
    EXPORT(vga_set_output_sink);

    /* GFX */
    EXPORT(gfx_get_info);
    EXPORT(gfx_list_display_modes);
    EXPORT(gfx_list_all_display_modes);

    /* Program helpers */
    EXPORT(deskmanager_set_background);
    EXPORT(deskmanager_get_background);
    EXPORT(deskmanager_get_wallpaper_path);
    EXPORT(deskmanager_set_wallpaper);
    EXPORT(elf_last_error);
    EXPORT(elf_preview_create);
    EXPORT(elf_preview_destroy);
    EXPORT(pe_dump_info);
    EXPORT(pe_execute_program);
    EXPORT(pe_last_error);
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

uint32_t elf_user_api_thunk(const char *name, uint32_t target) {
    uint8_t *code;
    uint32_t token;

    if (!target || elf_symbol_is_shared_data(name)) return target;
    for (token = 0; token < g_user_thunk_count; token++) {
        if (g_user_targets[token] == target)
            return (uint32_t)(uintptr_t)g_user_thunks[token];
    }
    if (g_user_thunk_count >= ELF_USER_THUNK_MAX) return 0;
    token = g_user_thunk_count++;
    g_user_targets[token] = target;
    code = g_user_thunks[token];
    kmemset(code, 0x90, ELF_USER_THUNK_SIZE);
    code[0] = 0xB8; /* mov eax, SYS_API_CALL */
    *(uint32_t *)(void *)(code + 1) = SYS_API_CALL;
    /* ECX es caller-saved en i386. EBX no lo es: usarlo para el token
     * corrompia el estado que las aplicaciones conservan entre llamadas. */
    code[5] = 0xB9; /* mov ecx, token */
    *(uint32_t *)(void *)(code + 6) = token;
    code[10] = 0xCD; code[11] = 0x80; /* int 0x80 */
    code[12] = 0xC3;                  /* ret */
    return (uint32_t)(uintptr_t)code;
}

uint64_t elf_user_api_dispatch(uint32_t token, const uint32_t *a,
                               bool *valid, uint32_t *callee_cleanup) {
    extern uint64_t elf_api_call_raw(uint32_t target, const uint32_t *args,
                                     uint32_t *callee_cleanup);

    if (valid) *valid = false;
    if (!a || token >= g_user_thunk_count || !g_user_targets[token]) return 0;
    if (valid) *valid = true;
    return elf_api_call_raw(g_user_targets[token], a, callee_cleanup);
}

static uint32_t elf_symbol_value(const elf32_symbol_t *symbol,
                                 const char *strings,
                                 const uint32_t *section_addresses,
                                 uint16_t section_count, bool user_image) {
    if (symbol->shndx == SHN_UNDEF) {
        const char *name = strings + symbol->name;
        uint32_t target = elf_kernel_symbol(name);
        return user_image ? elf_user_api_thunk(name, target) : target;
    }
    if (symbol->shndx == SHN_ABS) return symbol->value;
    if (symbol->shndx >= section_count ||
        section_addresses[symbol->shndx] == 0) return 0;
    return section_addresses[symbol->shndx] + symbol->value;
}

static bool elf_validate(const elf32_header_t *header, uint32_t size) {
    if (!header || size < sizeof(*header)) return false;
    if (header->ident[0] != 0x7F || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != 1 || header->ident[5] != 1 ||
        header->type != ET_REL || header->machine != EM_386 ||
        header->version != EV_CURRENT ||
        header->shentsize != sizeof(elf32_section_t) ||
        header->shnum == 0) return false;
    return elf_range_ok(header->shoff,
                        (uint32_t)header->shnum * header->shentsize, size);
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
    const elf32_section_t *symtab_section = NULL;
    const elf32_symbol_t *symbols = NULL;
    const char *strings = NULL;
    uint32_t symbol_count = 0;

    if (!entry_name || !image_out || !entry_out ||
        !elf_validate(header, file_size)) {
        g_elf_error = "ELF32 ET_REL invalido";
        return false;
    }
    sections = (const elf32_section_t *)(file + header->shoff);
    addresses = (uint32_t *)kzalloc(header->shnum * sizeof(uint32_t));
    if (!addresses) {
        g_elf_error = "sin memoria para secciones ELF";
        return false;
    }

    for (uint16_t i = 0; i < header->shnum; i++) {
        if (!(sections[i].flags & SHF_ALLOC)) continue;
        if (sections[i].addralign > image_alignment)
            image_alignment = sections[i].addralign;
        image_size = elf_align(image_size,
                               sections[i].addralign ? sections[i].addralign : 1);
        addresses[i] = image_size;
        if (sections[i].size > 0xFFFFFFFFU - image_size) {
            kfree(addresses);
            g_elf_error = "imagen ELF demasiado grande";
            return false;
        }
        image_size += sections[i].size;
    }
    if (image_alignment & (image_alignment - 1U)) {
        kfree(addresses);
        g_elf_error = "alineacion ELF invalida";
        return false;
    }
    raw_image = (uint8_t *)kzalloc(image_size + image_alignment - 1U +
                                   sizeof(void *));
    if (!raw_image) {
        kfree(addresses);
        g_elf_error = "sin memoria para cargar programa";
        return false;
    }
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
            sections[i].link >= header->shnum) goto malformed;
        const elf32_section_t *strtab = &sections[sections[i].link];
        if (!elf_range_ok(strtab->offset, strtab->size, file_size)) goto malformed;
        symtab_section = &sections[i];
        symbols = (const elf32_symbol_t *)(file + sections[i].offset);
        strings = (const char *)(file + strtab->offset);
        symbol_count = sections[i].size / sizeof(elf32_symbol_t);
        break;
    }
    if (!symbols || !strings) {
        g_elf_error = "ELF sin tabla de simbolos";
        goto fail;
    }

    for (uint32_t i = 0; i < symbol_count; i++) {
        if (symbols[i].shndx == SHN_UNDEF &&
            symbols[i].name != 0 &&
            elf_symbol_value(&symbols[i], strings, addresses,
                             header->shnum, user_image) == 0) {
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
            if (symbol_index >= symbol_count ||
                rels[r].offset > target_size - sizeof(uint32_t)) goto malformed;
            place = (uint32_t *)(uintptr_t)
                (addresses[relsec->info] + rels[r].offset);
            symbol_value = elf_symbol_value(&symbols[symbol_index], strings,
                                            addresses, header->shnum,
                                            user_image);
            if (type == R_386_32) {
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

bool elf_execute_program_ex(const char *path, gui_desktop_t *desktop,
                            const char *launch_arg) {
    return elf_spawn_program_ex(path, desktop, launch_arg) >= 0;
}

int elf_spawn_program_ex(const char *path, gui_desktop_t *desktop,
                         const char *launch_arg) {
    void *file = NULL;
    uint32_t size = 0;
    int pid;

    if (!path || !desktop) return -1;
    if (g_loaded_entry && kstrcmp(path, g_loaded_path) == 0) {
        pid = task_create_user_program(elf_task_name(path),
                                       (task_entry_t)g_loaded_entry,
                                       desktop, launch_arg);
        if (pid < 0) {
            g_elf_error = "sin slots para crear proceso";
            return -1;
        }
        return pid;
    }
    if (!vfs_read_all(path, &file, &size)) {
        g_elf_error = "no se pudo leer el programa";
        return -1;
    }
    if (!elf_load((const uint8_t *)file, size, "bleskernos_program_main",
                  &g_loaded_image, (void **)&g_loaded_entry, true)) {
        kfree(file);
        return -1;
    }
    kfree(file);
    kstrncpy(g_loaded_path, path, sizeof(g_loaded_path) - 1);
    g_loaded_path[sizeof(g_loaded_path) - 1] = '\0';
    pid = task_create_user_program(elf_task_name(path),
                                   (task_entry_t)g_loaded_entry,
                                   desktop, launch_arg);
    if (pid < 0) {
        g_elf_error = "sin slots para crear proceso";
        return -1;
    }
    return pid;
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
    if (!elf_load((const uint8_t *)file, size, entry_symbol,
                  image_out, entry_out, false)) {
        kfree(file);
        return false;
    }
    kfree(file);
    return true;
}

void elf_release_image(void *image) {
    if (image) kfree(((void **)image)[-1]);
}

const char *elf_last_error(void) {
    return g_elf_error;
}
