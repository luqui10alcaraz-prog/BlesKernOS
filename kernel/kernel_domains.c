#include "include/kernel_domains.h"
#include "include/klock.h"
#include "include/memory.h"
#include "include/task.h"
#include "include/smp.h"
#include "include/vga.h"

static kmutex_t g_domains[KDOMAIN_COUNT];
static volatile uint32_t g_order_violations;
/* One report bit per (requested, already-held) pair.  The counter still
 * records every violation, but COM1 is not allowed to turn a recoverable
 * ordering bug into a scheduler starvation storm. */
static volatile uint32_t g_order_reported[KDOMAIN_COUNT];

static uint32_t domain_cpu(void) {
    uint32_t cpu = smp_cpu_index();
    return cpu < SMP_MAX_CPUS ? cpu : 0U;
}

static uint16_t domain_depth_owned(uint32_t index, uint32_t owner) {
    if (index >= KDOMAIN_COUNT || !owner) return 0U;
    return kmutex_depth_owned(&g_domains[index], owner);
}

static int domain_highest_held(uint32_t owner) {
    for (int i = (int)KDOMAIN_COUNT - 1; i >= 0; i--)
        if (domain_depth_owned((uint32_t)i, owner)) return i;
    return -1;
}

static bool domain_report_pair_once(uint32_t requested, uint32_t held) {
    uint32_t bit;
    uint32_t old;
    if (requested >= KDOMAIN_COUNT || held >= KDOMAIN_COUNT) return false;
    bit = 1U << held;
    old = __sync_fetch_and_or(&g_order_reported[requested], bit);
    return (old & bit) == 0U;
}

static void domain_check_order(uint32_t index) {
    uint32_t owner = klock_current_owner_token();
    int highest = domain_highest_held(owner);
    if (!domain_depth_owned(index, owner) && highest > (int)index) {
        __asm__ volatile ("lock incl %0" : "+m"(g_order_violations) : :
                          "memory", "cc");
        if (domain_report_pair_once(index, (uint32_t)highest)) {
            kprintf("[LOCK] inversion CPU%u tid=%u: %s despues de %s "
                    "(se ocultan repeticiones)\n",
                    domain_cpu(), task_current_pid(),
                    kernel_domain_name(index),
                    kernel_domain_name((uint32_t)highest));
        }
    }
}

static bool text_starts(const char *text, const char *prefix) {
    if (!text || !prefix) return false;
    while (*prefix) {
        if (*text++ != *prefix++) return false;
    }
    return true;
}

static bool text_equals(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static bool is_upper_ascii(char c) {
    return c >= 'A' && c <= 'Z';
}

void kernel_domains_init(void) {
    for (uint32_t i = 0U; i < KDOMAIN_COUNT; i++) kmutex_init(&g_domains[i]);
    kmemset((void *)g_order_reported, 0, sizeof(g_order_reported));
    g_order_violations = 0U;
}

const char *kernel_domain_name(uint32_t index) {
    static const char *names[KDOMAIN_COUNT] = {
        "task", "vfs", "gui", "gfx", "net", "audio", "driver",
        "wine", "legacy"
    };
    return index < KDOMAIN_COUNT ? names[index] : "?";
}

uint32_t kernel_domain_mask_for_api(const char *name) {
    if (!name || !name[0]) return KDOMAIN_LEGACY;

    /* Pure helpers operate only on caller-owned storage. Keeping them outside
     * every subsystem lock matters for render loops and codecs. */
    if (text_starts(name, "kmem") || text_starts(name, "kstr") ||
        text_starts(name, "mem") || text_starts(name, "str") ||
        text_starts(name, "bk_string_") ||
        text_equals(name, "bk_memory_compare") ||
        text_starts(name, "bk_data_") || text_starts(name, "math_") ||
        text_starts(name, "fabs") || text_starts(name, "sqrt") ||
        text_starts(name, "sin") || text_starts(name, "cos") ||
        text_starts(name, "tan"))
        return 0U;

    if (text_starts(name, "bk_proc_") || text_starts(name, "bk_thread_") ||
        text_starts(name, "task_") || text_starts(name, "process_") ||
        text_starts(name, "perfmon_") ||
        text_starts(name, "bk_perf_") || text_starts(name, "bk_sys_alloc") ||
        text_starts(name, "bk_sys_free") ||
        text_starts(name, "bk_sys_memory_info"))
        return KDOMAIN_TASK;

    if (text_starts(name, "bk_app_launch"))
        return KDOMAIN_TASK | KDOMAIN_VFS | KDOMAIN_GUI;

    if (text_starts(name, "vfs_") || text_starts(name, "fat_") ||
        text_starts(name, "bfs_") || text_starts(name, "iso9660_") ||
        text_starts(name, "bk_file_") || text_starts(name, "bk_fs_") ||
        text_starts(name, "bk_setup_") ||
        text_starts(name, "file_dialog_") || text_starts(name, "fopen") ||
        text_starts(name, "fread") || text_starts(name, "fwrite") ||
        text_starts(name, "fclose") || text_starts(name, "opendir") ||
        text_starts(name, "readdir"))
        return KDOMAIN_VFS;

    if (text_starts(name, "bk_gui_create_") ||
        text_starts(name, "bk_gui_set_window_") ||
        text_starts(name, "bk_gui_close_window") ||
        text_starts(name, "bk_gui_destroy_window") ||
        text_starts(name, "bk_gui_add_menu") ||
        text_starts(name, "bk_gui_window_") ||
        text_starts(name, "bk_gui_widget_") ||
        text_starts(name, "bk_gui_scrollbar_") ||
        text_starts(name, "bk_gui_request_paint") ||
        text_starts(name, "bk_clipboard_") || text_starts(name, "bk_lang_") ||
        text_starts(name, "bk_gui_widget_") ||
        text_starts(name, "gui_window_") || text_starts(name, "gui_widget_") ||
        text_starts(name, "gui_desktop_") || text_starts(name, "desktop_") ||
        text_starts(name, "deskmanager_") || text_starts(name, "deskbar_") ||
        text_starts(name, "clipboard_") || text_starts(name, "screensaver_"))
        return KDOMAIN_GUI;

    /* The graphics resource cache has its own mutex and returns a private
     * decoded copy to the caller.  Keeping the coarse GFX domain around PNG
     * decoding blocks the compositor for hundreds of milliseconds when a
     * large app (3D Plus) loads its toolbar and can deadlock with VFS on the
     * first package read. */
    if (text_starts(name, "bk_graphics_"))
        return 0U;

    if (text_starts(name, "bk_gui_surface_") ||
        text_starts(name, "bk_gui_image_") ||
        text_starts(name, "bk_gui_text_") ||
        text_starts(name, "bk_gui_cursor_") ||
        text_starts(name, "bk_mesa_") ||
        text_starts(name, "bk_tinygl_") ||
        text_starts(name, "gfx_") ||
        text_starts(name, "gfx3d_") || text_starts(name, "gui_gfx_") ||
        text_starts(name, "gl") || text_starts(name, "OSMesa") ||
        text_starts(name, "Mesa") || text_starts(name, "tinygl_") ||
        text_starts(name, "image_") || text_starts(name, "jpeg_") ||
        text_starts(name, "svg_") || text_starts(name, "font_"))
        return KDOMAIN_GFX;

    /* All remaining native GUI and input entry points operate on desktop,
     * window or event state.  In particular bk_gui_desktop used to fall into
     * LEGACY, which created a reverse LEGACY -> GUI/GFX dependency on every
     * application start. */
    if (text_starts(name, "bk_gui_") || text_starts(name, "bk_input_"))
        return KDOMAIN_GUI;

    if (text_starts(name, "bk_net_") || text_starts(name, "network_") ||
        text_starts(name, "net_") ||
        text_starts(name, "socket_") || text_starts(name, "http_") ||
        text_starts(name, "https_") || text_starts(name, "tls_") ||
        text_starts(name, "dns_") || text_starts(name, "dhcp_"))
        return KDOMAIN_NET;

    if (text_starts(name, "bk_sound_") || text_starts(name, "sound_") ||
        text_starts(name, "audio_") ||
        text_starts(name, "midi_") || text_starts(name, "speaker_"))
        return KDOMAIN_AUDIO;

    if (text_starts(name, "bk_device_volume_") ||
        text_starts(name, "bk_device_check_volume") ||
        text_starts(name, "bk_device_repair_volume") ||
        text_starts(name, "bk_device_mount_volume") ||
        text_starts(name, "bk_device_format_fat"))
        return KDOMAIN_VFS | KDOMAIN_DRIVER;

    if (text_starts(name, "bk_device_") || text_starts(name, "bk_print_") ||
        text_starts(name, "bk_console_") ||
        text_starts(name, "driver_") || text_starts(name, "pci_") ||
        text_starts(name, "block_") || text_starts(name, "ata_") ||
        text_starts(name, "usb_") || text_starts(name, "rtc_"))
        return KDOMAIN_DRIVER;

    /* Win32 exports use the conventional capitalized names. USER/GDI calls
     * also touch the native desktop and graphics state, so acquire those
     * domains in the global order before the Wine compatibility lock. */
    if (is_upper_ascii(name[0])) {
        uint32_t mask = KDOMAIN_WINE;
        if (text_starts(name, "CreateWindow") || text_starts(name, "DestroyWindow") ||
            text_starts(name, "ShowWindow") || text_starts(name, "SetWindow") ||
            text_starts(name, "GetWindow") || text_starts(name, "SendMessage") ||
            text_starts(name, "PostMessage") || text_starts(name, "DispatchMessage") ||
            text_starts(name, "DefWindowProc") || text_starts(name, "DialogBox") ||
            text_starts(name, "EndDialog"))
            mask |= KDOMAIN_GUI;
        if (text_starts(name, "BitBlt") || text_starts(name, "StretchBlt") ||
            text_starts(name, "CreateDC") || text_starts(name, "DeleteDC") ||
            text_starts(name, "SelectObject") || text_starts(name, "TextOut") ||
            text_starts(name, "SetPixel") || text_starts(name, "GetPixel"))
            mask |= KDOMAIN_GFX;
        if (text_starts(name, "CreateFile") || text_starts(name, "ReadFile") ||
            text_starts(name, "WriteFile") || text_starts(name, "CloseHandle") ||
            text_starts(name, "FindFirstFile") || text_starts(name, "FindNextFile") ||
            text_starts(name, "DeleteFile") || text_starts(name, "MoveFile"))
            mask |= KDOMAIN_VFS;
        return mask;
    }

    if (text_starts(name, "bk_sys_ticks") ||
        text_starts(name, "bk_sys_uptime") ||
        text_starts(name, "bk_sys_getpid") ||
        text_starts(name, "bk_sys_sleep") ||
        text_starts(name, "bk_sys_yield") ||
        text_starts(name, "bk_sys_api_version") ||
        text_starts(name, "bk_sys_capabilities") ||
        text_starts(name, "bk_time_"))
        return 0U;

    if (text_starts(name, "bk_sys_reboot") ||
        text_starts(name, "bk_sys_shutdown") ||
        text_starts(name, "bk_sys_log"))
        return KDOMAIN_DRIVER;

    /* Known read-only clocks are per-CPU or atomic and need no gate. */
    if (text_equals(name, "pit_get_ticks") ||
        text_equals(name, "pit_get_frequency_hz") ||
        text_equals(name, "rtc_get_datetime"))
        return 0U;

    /* The old lock survives only as quarantine for unclassified third-party
     * entry points. Every classified native subsystem can run independently. */
    return KDOMAIN_LEGACY;
}

void kernel_domains_enter(uint32_t mask) {
    for (uint32_t i = 0U; i < KDOMAIN_COUNT; i++) {
        if (!(mask & (1U << i))) continue;
        domain_check_order(i);
        kmutex_lock(&g_domains[i]);
    }
}

void kernel_domains_exit(uint32_t mask) {
    for (uint32_t i = KDOMAIN_COUNT; i > 0U; i--) {
        uint32_t index = i - 1U;
        if (!(mask & (1U << index))) continue;
        kmutex_unlock(&g_domains[index]);
    }
}

void kernel_domains_drop_current(kernel_domain_snapshot_t *snapshot) {
    uint32_t owner = klock_current_owner_token();
    if (!snapshot) return;
    kmemset(snapshot, 0, sizeof(*snapshot));
    /* Release in reverse lock order, mirroring normal unlock. */
    for (uint32_t i = KDOMAIN_COUNT; i > 0U; i--) {
        uint32_t index = i - 1U;
        snapshot->depth[index] =
            kmutex_drop_owner(&g_domains[index], owner);
    }
}

void kernel_domains_restore(const kernel_domain_snapshot_t *snapshot) {
    if (!snapshot) return;
    for (uint32_t i = 0U; i < KDOMAIN_COUNT; i++) {
        if (!snapshot->depth[i]) continue;
        domain_check_order(i);
        kmutex_restore_depth(&g_domains[i], snapshot->depth[i]);
    }
}

void kernel_domains_abandon_owner(uint32_t owner) {
    if (!owner) return;
    for (uint32_t i = 0U; i < KDOMAIN_COUNT; i++)
        kmutex_abandon_owner(&g_domains[i], owner);
}

void kernel_domains_abandon_current(void) {
    kernel_domains_abandon_owner(klock_current_owner_token());
}

uint32_t kernel_domains_held_mask(void) {
    uint32_t owner = klock_current_owner_token();
    uint32_t mask = 0U;
    for (uint32_t i = 0U; i < KDOMAIN_COUNT; i++)
        if (domain_depth_owned(i, owner)) mask |= 1U << i;
    return mask;
}

uint32_t kernel_domains_order_violations(void) {
    return g_order_violations;
}

void kernel_domains_dump_current(void) {
    uint32_t owner = klock_current_owner_token();
    uint32_t mask = kernel_domains_held_mask();
    uint32_t cpu = domain_cpu();
    kprintf("[LOCK] CPU%u tid=%u dominios=%x violaciones=%u",
            cpu, task_current_pid(), mask, g_order_violations);
    for (uint32_t i = 0U; i < KDOMAIN_COUNT; i++)
        if (mask & (1U << i))
            kprintf(" %s:%u", kernel_domain_name(i),
                    domain_depth_owned(i, owner));
    kprintf("\n");
}
