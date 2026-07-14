#include "../kernel/include/api.h"

#define ABOUT_ICON_BYTES (28U * 28U * sizeof(uint32_t))

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    uint32_t *icon;
    gui_gif_animation_t logo;
    uint16_t logo_frame;
    uint32_t logo_next_tick;
    char cpu[49];
    char ram[64];
} about_state_t;

static about_state_t *g_about;

static bool about_load_logo(about_state_t *st) {
    if (!st) return false;
    return bk_gui_gif_load_animation(&st->logo, "/ABOUNT.GIF");
}

static void about_cpuid(uint32_t leaf, uint32_t *a, uint32_t *b,
                        uint32_t *c, uint32_t *d) {
    __asm__ volatile ("cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf), "c"(0));
}

static void about_cpu_name(char *out) {
    uint32_t a, b, c, d;
    about_cpuid(0x80000000, &a, &b, &c, &d);
    if (a >= 0x80000004) {
        uint32_t *words = (uint32_t *)out;
        for (uint32_t leaf = 0; leaf < 3; leaf++) {
            about_cpuid(0x80000002 + leaf, &a, &b, &c, &d);
            words[leaf * 4] = a;
            words[leaf * 4 + 1] = b;
            words[leaf * 4 + 2] = c;
            words[leaf * 4 + 3] = d;
        }
        out[48] = '\0';
        while (*out == ' ') {
            for (int i = 0; i < 48; i++) out[i] = out[i + 1];
        }
        return;
    }
    about_cpuid(0, &a, &b, &c, &d);
    ((uint32_t *)out)[0] = b;
    ((uint32_t *)out)[1] = d;
    ((uint32_t *)out)[2] = c;
    out[12] = '\0';
}

static void about_number(char *out, uint32_t value) {
    char digits[12];
    int pos = 11;
    digits[pos] = '\0';
    if (!value) digits[--pos] = '0';
    while (value) {
        digits[--pos] = (char)('0' + value % 10);
        value /= 10;
    }
    bk_runtime_strcpy(out, &digits[pos]);
}

static void about_kb_text(char *out, uint32_t bytes) {
    char digits[12];
    about_number(digits, (bytes + 1023U) / 1024U);
    bk_runtime_strcpy(out, digits);
    bk_runtime_strcat(out, " KB");
}

static void about_size_text(char *out, uint32_t bytes) {
    char digits[12];

    if (bytes > MEMORY_DISPLAY_MB_THRESHOLD) {
        about_number(digits, (bytes + ((1024U * 1024U) - 1U)) / (1024U * 1024U));
        bk_runtime_strcpy(out, digits);
        bk_runtime_strcat(out, " MB");
        return;
    }

    about_kb_text(out, bytes);
}

static void about_ram_text(char *out) {
    system_memory_info_t info;
    char total[16];
    char free[16];

    mm_get_system_info(&info);
    about_size_text(total, (uint32_t)info.total_bytes);
    about_size_text(free, (uint32_t)info.free_bytes);

    bk_runtime_strcpy(out, total);
    bk_runtime_strcat(out, " total / ");
    bk_runtime_strcat(out, free);
    bk_runtime_strcat(out, " libres");
}

static uint32_t about_logo_bytes(const about_state_t *st) {
    uint32_t total = 0;

    if (!st) return 0;
    if (st->logo.frames)
        total += (uint32_t)st->logo.frame_count * sizeof(gui_image_t);
    if (st->logo.delays_cs)
        total += (uint32_t)st->logo.frame_count * sizeof(uint16_t);
    for (uint16_t i = 0; i < st->logo.frame_count; i++) {
        if (!st->logo.frames[i].pixels) continue;
        total += (uint32_t)st->logo.frames[i].width *
                 (uint32_t)st->logo.frames[i].height *
                 sizeof(uint32_t);
    }
    return total;
}

static const gui_image_t *about_current_logo(const about_state_t *st) {
    if (!st || !st->logo.frames || !st->logo.frame_count) return NULL;
    if (st->logo_frame >= st->logo.frame_count) return &st->logo.frames[0];
    return &st->logo.frames[st->logo_frame];
}

static uint32_t about_frame_delay_ticks(const about_state_t *st, uint16_t frame) {
    uint32_t pit_hz;
    uint32_t delay_cs;
    uint32_t ticks;

    if (!st || !st->logo.delays_cs || frame >= st->logo.frame_count) return 1U;
    pit_hz = bk_sys_tick_frequency();
    if (!pit_hz) pit_hz = 100U;
    delay_cs = st->logo.delays_cs[frame];
    if (!delay_cs) delay_cs = 10U;
    ticks = (delay_cs * pit_hz + 99U) / 100U;
    return ticks ? ticks : 1U;
}

static bool about_advance_logo(about_state_t *st, uint32_t now) {
    bool advanced = false;

    if (!st || st->logo.frame_count <= 1) return false;
    if (!st->logo_next_tick)
        st->logo_next_tick = now + about_frame_delay_ticks(st, st->logo_frame);

    while ((int32_t)(now - st->logo_next_tick) >= 0) {
        st->logo_frame = (uint16_t)((st->logo_frame + 1U) % st->logo.frame_count);
        st->logo_next_tick += about_frame_delay_ticks(st, st->logo_frame);
        advanced = true;
    }
    return advanced;
}

static int about_centered_text_x(int area_x, int area_w, const char *text) {
    int text_w;

    if (!text || area_w <= 0) return area_x;
    text_w = (int)bk_gui_font_text_width(text);
    if (text_w >= area_w) return area_x;
    return area_x + ((area_w - text_w) / 2);
}

static void about_content(gui_window_t *window UNUSED,
                          gui_surface_t *surface, void *context) {
    about_state_t *st = (about_state_t *)context;
    gui_rect_t content_clip;
    gui_rect_t info_box;
    gui_rect_t info_clip;
    int margin = 18;
    int x;
    int y;
    int w;
    int h;
    const gui_image_t *logo;
    int logo_w;
    int logo_h;
    int logo_x;
    int info_y;
    int footer_y;

    if (!st || !st->window || !st->window->visible) return;
    x = st->window->bounds.x + margin;
    y = st->window->bounds.y + GUI_TITLEBAR_HEIGHT + 14;
    w = st->window->bounds.w - (margin * 2);
    h = st->window->bounds.h - GUI_TITLEBAR_HEIGHT - 18;
    if (w <= 0 || h <= 0) return;

    content_clip = (gui_rect_t){x, y, w, h};
    logo = about_current_logo(st);
    logo_w = logo ? (int)logo->width : 28;
    logo_h = logo ? (int)logo->height : 28;
    logo_x = x + ((w - logo_w) / 2);
    if (logo_x < x) logo_x = x;

    if (logo && logo->pixels) {
        bk_app_draw_icon(surface, logo_x, y, logo->pixels,
                                 logo->width, logo->height);
    } else if (st->icon) {
        bk_app_draw_icon(surface, logo_x, y, st->icon, 28, 28);
    } else {
        bk_gui_gfx_fill_rect(surface, (gui_rect_t){logo_x, y, 28, 28}, 0x00204070);
        bk_gui_font_draw_string_scaled(surface, logo_x + 7, y + 6, "B",
                                    0x00FFFFFF, 2);
    }

    y += logo_h + 10;
    info_box = (gui_rect_t){x + 10, y, w - 20, 58};
    if (info_box.w > 0 && info_box.h > 0) {
        bk_gui_gfx_fill_rect(surface, info_box, 0x00FFFFFF);
        bk_gui_gfx_draw_rect(surface, info_box, 0x0090A0B0);
        bk_gui_gfx_fill_rect(surface,
                          (gui_rect_t){info_box.x + 1, info_box.y + 1,
                                       info_box.w - 2, 1},
                          0x00F4F7FA);
        info_clip = (gui_rect_t){info_box.x + 10, info_box.y + 6,
                                 info_box.w - 20, info_box.h - 12};
        info_y = info_box.y + 6;
        bk_gui_font_draw_string(surface, info_box.x + 10, info_y,
                             "Kernel:", 0x00203040, 0, false);
        bk_gui_font_draw_string_clipped(surface, info_box.x + 62, info_y,
                                     "BK1", 0x00203040, info_clip);
        bk_gui_font_draw_string(surface, info_box.x + 10, info_y + 18,
                             "CPU:", 0x00203040, 0, false);
        bk_gui_font_draw_string_clipped(surface, info_box.x + 62, info_y + 18,
                                     st->cpu, 0x00203040, info_clip);
        bk_gui_font_draw_string(surface, info_box.x + 10, info_y + 32,
                             "RAM:", 0x00203040, 0, false);
        bk_gui_font_draw_string_clipped(surface, info_box.x + 62, info_y + 32,
                                     st->ram, 0x00203040, info_clip);
    }

    footer_y = st->window->bounds.y + st->window->bounds.h - 16;
    bk_gui_font_draw_string_clipped(surface,
                                 about_centered_text_x(x, w,
                                                       "Bles.INC (C) 2026"),
                                 footer_y, "Bles.INC (C) 2026",
                                 0x00304050, content_clip);
}

static void about_cleanup(about_state_t *st) {
    if (!st) return;
    if (st->window) {
        bk_gui_desktop_remove_window(st->desktop, st->window);
        bk_gui_window_destroy_raw(st->window);
        bk_proc_bind_window(NULL);
        st->window = NULL;
    }
    if (st->icon) bk_sys_free(st->icon);
    bk_gui_gif_animation_free(&st->logo);
    if (g_about == st) g_about = NULL;
    bk_sys_free(st);
}

static void about_main(void *argument) {
    about_state_t *st = (about_state_t *)argument;
    if (!st || !st->desktop) {
        about_cleanup(st);
        bk_proc_exit();
    }

    bk_proc_set_memory_hint(sizeof(*st) + (st->icon ? ABOUT_ICON_BYTES : 0) +
                         about_logo_bytes(st));
    st->window = bk_gui_create_window(
        st->desktop, 125, 58, 390, st->logo.frame_count ? 250 : 165,
        "Sobre BlesKernOS 1.0");
    if (st->window) {
        bk_gui_set_window_content(st->window, about_content, st);
        bk_gui_set_window_min_size(st->window,
                                st->logo.frame_count ? 300 : 260,
                                st->logo.frame_count ? 250 : 140);
        st->window->owner_pid = bk_sys_getpid();
        bk_proc_bind_window(st->window);
    }
    if (st->logo.frame_count > 1)
        st->logo_next_tick = bk_sys_ticks() + about_frame_delay_ticks(st, 0);

    while (!bk_proc_exit_requested()) {
        if (!st->window || !st->window->listed) break;
        if (about_advance_logo(st, bk_sys_ticks()) && st->window)
            st->window->dirty = true;
        about_ram_text(st->ram);
        bk_sys_sleep_ticks(st->logo.frame_count > 1 ? 1U : 8U);
    }

    about_cleanup(st);
    bk_proc_exit();
}

bool about_get_runtime_info(program_runtime_info_t *info) {
    if (!info || !g_about) return false;
    info->window = g_about->window;
    info->memory_bytes = sizeof(*g_about) +
                         (g_about->icon ? ABOUT_ICON_BYTES : 0) +
                         about_logo_bytes(g_about) +
                         (g_about->window ? (uint32_t)sizeof(gui_window_t) : 0);
    return true;
}

void about_open(gui_desktop_t *desktop) {
    about_state_t *st;

    if (!desktop) return;
    if (g_about) {
        if (g_about->window) {
            if (!g_about->window->visible)
                bk_gui_window_restore(g_about->window);
            bk_gui_desktop_raise_window(desktop, g_about->window);
            g_about->window->dirty = true;
        }
        return;
    }

    st = (about_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    about_cpu_name(st->cpu);
    about_ram_text(st->ram);
    (void)about_load_logo(st);
    g_about = st;
    if (bk_proc_spawn_thread("about", about_main, st) < 0) {
        about_cleanup(st);
    }
}

void about_install(gui_desktop_t *desktop UNUSED) {}

void bleskernos_program_main(gui_desktop_t *desktop) {
    about_state_t *st;

    if (!desktop) {
        bk_proc_exit();
    }

    if (g_about) {
        if (g_about->window) {
            if (!g_about->window->visible)
                bk_gui_window_restore(g_about->window);
            bk_gui_desktop_raise_window(desktop, g_about->window);
            g_about->window->dirty = true;
        }
        return;
    }

    st = (about_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) {
        bk_proc_exit();
    }

    st->desktop = desktop;
    about_cpu_name(st->cpu);
    about_ram_text(st->ram);
    (void)about_load_logo(st);
    g_about = st;

    /*
     * Como ELF externo, About mantiene su propia ventana en esta tarea.
     * Evitamos depender de una segunda tarea hija para que no desaparezca
     * apenas termina la entrada del programa.
     */
    about_main(st);
}
