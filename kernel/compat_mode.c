#include "include/compat_mode.h"
#include "include/memory.h"
#include "include/vga.h"

#define BK_MIB(value) ((uint32_t)(value) * 1024U * 1024U)
#define BK_STACK_TINY  (32U * 1024U)
#define BK_STACK_KERNEL (48U * 1024U)
#define BK_STACK_USER   (64U * 1024U)
#define BK_STACK_LARGE  (128U * 1024U)
#define BK_STACK_QUAKE  (512U * 1024U)

static bk_memory_profile_t g_profile = BK_MEMORY_PROFILE_NORMAL;
static uint32_t g_physical_top;
static uint32_t g_task_limit = 32U;
static bool g_legacy_p6_cpu;
static uint32_t g_cpu_family;
static uint32_t g_cpu_model;

static bool compat_cpu_has_cpuid(void) {
    uint32_t before;
    uint32_t after;
    uint32_t toggled;

    __asm__ volatile ("pushfl; popl %0" : "=r"(before));
    toggled = before ^ (1U << 21);
    __asm__ volatile ("pushl %0; popfl" : : "r"(toggled) : "cc", "memory");
    __asm__ volatile ("pushfl; popl %0" : "=r"(after));
    __asm__ volatile ("pushl %0; popfl" : : "r"(before) : "cc", "memory");
    return ((before ^ after) & (1U << 21)) != 0U;
}

static void compat_detect_cpu(void) {
    uint32_t eax = 0U, ebx, ecx, edx;
    uint32_t base_family;
    uint32_t base_model;

    g_cpu_family = 3U;
    g_cpu_model = 0U;
    g_legacy_p6_cpu = true;
    if (!compat_cpu_has_cpuid()) return;
    __asm__ volatile ("cpuid"
                      : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < 1U) return;
    eax = 1U;
    __asm__ volatile ("cpuid"
                      : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    base_family = (eax >> 8) & 0xFU;
    base_model = (eax >> 4) & 0xFU;
    g_cpu_family = base_family;
    if (base_family == 0xFU) g_cpu_family += (eax >> 20) & 0xFFU;
    g_cpu_model = base_model;
    if (base_family == 0x6U || base_family == 0xFU)
        g_cpu_model |= ((eax >> 16) & 0xFU) << 4;

    /* Pentium Pro/II/III y Athlon clásicos: familia 6, modelos hasta 11.
     * También conservar un presupuesto prudente para CPUs pre-P6. */
    g_legacy_p6_cpu = g_cpu_family < 6U ||
        (g_cpu_family == 6U && g_cpu_model <= 11U);
}

static char compat_lower(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

static bool compat_name_contains(const char *name, const char *needle) {
    if (!name || !needle || !*needle) return false;
    for (; *name; name++) {
        const char *left = name;
        const char *right = needle;
        while (*left && *right && compat_lower(*left) == compat_lower(*right)) {
            left++;
            right++;
        }
        if (!*right) return true;
    }
    return false;
}

void compat_mode_init(void) {
    compat_detect_cpu();
    g_physical_top = mm_physical_top();
    if (g_physical_top <= BK_MIB(7)) {
        g_profile = BK_MEMORY_PROFILE_TINY;
        if (g_physical_top <= BK_MIB(4)) g_task_limit = 2U;
        else if (g_physical_top <= BK_MIB(5)) g_task_limit = 3U;
        else g_task_limit = 5U;
    } else if (g_physical_top <= BK_MIB(15)) {
        g_profile = BK_MEMORY_PROFILE_LOW;
        /* Dos pilas de 32 KiB por programa permiten unas diez aplicaciones
         * ligeras sin comprometer el framebuffer ni los drivers residentes. */
        g_task_limit = 12U;
    } else {
        g_profile = BK_MEMORY_PROFILE_NORMAL;
        /* Native thread pools need enough task slots to expose all CPUs.
         * Keep a conservative ceiling on 16-31 MiB systems, but use the full
         * TASK_MAX-compatible capacity once at least 32 MiB are available. */
        g_task_limit = g_physical_top < BK_MIB(32) ? 16U : 32U;
    }

    kprintf("[COMPAT] perfil=%s RAM_top=%u KiB heap=%u KiB tareas=%u "
            "CPU=fam%u/mod%u GUI=%uFPS TIMER=%uHz\n",
            compat_mode_name(), g_physical_top / 1024U,
            (uint32_t)(mm_heap_size() / 1024U), g_task_limit,
            g_cpu_family, g_cpu_model, compat_mode_gui_target_fps(),
            compat_mode_timer_hz());
    if (g_profile == BK_MEMORY_PROFILE_LOW) {
        kprintf("[COMPAT] perfil recomendado 8MB: 640x480x256, drivers "
                "completos y servicios pesados bajo demanda\n");
    } else if (g_profile == BK_MEMORY_PROFILE_TINY) {
        kprintf("[COMPAT] perfil 4-7MB: escritorio basico y servicios "
                "pesados diferidos\n");
    }
}

bk_memory_profile_t compat_mode_profile(void) { return g_profile; }
bool compat_mode_is_low_memory(void) { return g_profile != BK_MEMORY_PROFILE_NORMAL; }
bool compat_mode_is_tiny(void) { return g_profile == BK_MEMORY_PROFILE_TINY; }
/* The C600's useful target is the 8-15 MiB profile.  At that size a linear
 * 16/32-bit desktop buffer costs more than the services it replaces, and
 * software presentation dominates the Pentium III. Keep the GUI alive, but
 * use the small VGA path and make everything else on-demand. */
bool compat_mode_force_vga13h(void) { return compat_mode_is_tiny(); }
bool compat_mode_force_vga12h(void) { return g_profile == BK_MEMORY_PROFILE_LOW; }
bool compat_mode_allow_optional_services(void) { return g_profile == BK_MEMORY_PROFILE_NORMAL; }
bool compat_mode_allow_user_programs(void) {
    return !(compat_mode_is_tiny() && g_physical_top <= BK_MIB(4));
}
/* Los PE con .reloc pueden vivir en el heap reducido. Los ejecutables viejos
 * sin relocalizaciones seguirán fallando limpiamente si requieren la vista
 * fija 4-8 MiB, que en una máquina de 8 MiB pertenece al heap. */
bool compat_mode_allow_pe(void) { return g_profile != BK_MEMORY_PROFILE_TINY; }
/* El fondo CLASSIC es un mosaico de 50x50: en el perfil de 8 MB se conserva
 * como fuente pequeña, sin el antiguo cache ARGB de toda la pantalla. */
bool compat_mode_allow_wallpaper(void) { return g_profile != BK_MEMORY_PROFILE_TINY; }
/* En 8-15 MiB los iconos se cargan escalados y bajo demanda (~3 KiB cada
 * uno). Solo el perfil tiny los omite para no consumir su heap minimo. */
bool compat_mode_allow_icon_images(void) {
    return g_profile != BK_MEMORY_PROFILE_TINY;
}
bool compat_mode_defer_driver(const char *path) {
    static const char *const deferred[] = {
        "/SYSTEM/DRIVERS/ATIR1283D.DVR",
        "/SYSTEM/DRIVERS/NETSTACK.DVR",
        "/SYSTEM/DRIVERS/SB16.DVR",
        "/SYSTEM/DRIVERS/TLS.DVR",
        "/SYSTEM/DRIVERS/USBCLASS.DVR",
        "/SYSTEM/DRIVERS/VMWARESVGA3D.DVR"
    };
    if (!compat_mode_is_low_memory() || !path) return false;
    for (uint32_t i = 0U; i < sizeof(deferred) / sizeof(deferred[0]); i++)
        if (kstrcmp(path, deferred[i]) == 0) return true;
    return false;
}
bool compat_mode_allow_screensaver(void) {
    /* Its Ring-3 full-screen cache costs 1.2 MiB already at 640x480. Keep the
       daemon available on disk, but do not make that cache resident on the
       16 MiB compatibility tier. */
    return g_physical_top > BK_MIB(16);
}
bool compat_mode_allow_startup_sound(void) { return g_profile == BK_MEMORY_PROFILE_NORMAL; }
bool compat_mode_use_compact_language(void) { return compat_mode_is_low_memory(); }
bool compat_mode_use_front_shadow(void) {
    /* 8-bpp presentation already converts from the ARGB desktop buffer. A
       second full ARGB copy only serves dirty comparison and prevents mode
       changes from fitting on nominal 16 MiB machines. */
    return g_profile == BK_MEMORY_PROFILE_NORMAL &&
           g_physical_top > BK_MIB(16);
}
bool compat_mode_prefer_800x600(void) {
    return g_profile == BK_MEMORY_PROFILE_NORMAL;
}
uint32_t compat_mode_task_limit(void) { return g_task_limit; }

uint32_t compat_mode_stack_size(const char *name, bool user) {
    bool heavy;

    if (compat_mode_is_low_memory()) return BK_STACK_TINY;

    /* El refresco software de WinQuake 1.09 documenta una cadena de llamadas
     * cercana a 256 KiB. Sumamos margen para el hilo y el adaptador nativo;
     * sólo el Ring 3 de Quake recibe este tamaño. */
    if (user && compat_name_contains(name, "quake"))
        return BK_STACK_QUAKE;

    heavy = compat_name_contains(name, "doom") ||
            compat_name_contains(name, "netsurf") ||
            compat_name_contains(name, "freeciv") ||
            compat_name_contains(name, "wine") ||
            compat_name_contains(name, "3d");

    /* A native application executes public API implementations on its kernel
     * stack while inside int 0x80. Previously 3D Plus received 128 KiB for
     * Ring 3 but only 48 KiB for those deep renderer/GUI call chains. Under a
     * sustained render that could overwrite the syscall frame itself. */
    if (heavy && g_profile == BK_MEMORY_PROFILE_NORMAL)
        return BK_STACK_LARGE;
    if (!user) return BK_STACK_KERNEL;
    return BK_STACK_USER;
}

uint32_t compat_mode_gui_target_fps(void) {
    if (compat_mode_is_tiny()) return 20U;
    if (g_profile == BK_MEMORY_PROFILE_LOW) return 30U;
    /* Un PII/PIII no puede componer y subir una pantalla grande a 60 FPS sin
     * consumir el CPU entero. Los clics siguen siendo urgentes; el límite sólo
     * regula animaciones y movimiento continuo. */
    if (g_legacy_p6_cpu) return 30U;
    return 60U;
}

uint32_t compat_mode_gui_idle_hz(void) {
    if (compat_mode_is_tiny()) return 25U;
    if (g_profile == BK_MEMORY_PROFILE_LOW) return 50U;
    if (g_legacy_p6_cpu) return 60U;
    return 100U;
}

uint32_t compat_mode_timer_hz(void) {
    /* 100 Hz era una frecuencia habitual y suficiente para sistemas P6. En
       esos procesadores reduce 3x las IRQ del PIT y de cada LAPIC sin empeorar
       la respuesta interactiva: el quantum se calcula en milisegundos. */
    if (g_legacy_p6_cpu || compat_mode_is_low_memory()) return 100U;
    return 300U;
}

const char *compat_mode_name(void) {
    if (g_profile == BK_MEMORY_PROFILE_TINY) return "4-7MB";
    if (g_profile == BK_MEMORY_PROFILE_LOW) return "8-15MB";
    return "normal";
}
