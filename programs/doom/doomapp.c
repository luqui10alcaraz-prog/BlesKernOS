#include "../../kernel/include/api.h"
#include "../../kernel/stdio.h"
#include "doom_port.h"
#include "doomgeneric/doomgeneric.h"

#define DOOM_FB_WIDTH 640
#define DOOM_FB_HEIGHT 400
#define DOOM_WINDOW_W (DOOM_FB_WIDTH + 4)
#define DOOM_WINDOW_H (DOOM_FB_HEIGHT + GUI_TITLEBAR_HEIGHT + 2)

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    uint32_t *framebuffer;
    bool running;
    bool failed;
    char wad_path[64];
    char message[120];
} doom_state_t;

static doom_state_t *g_doom;

static void doom_copy_text(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    bk_runtime_strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static bool doom_find_wad(char *out, size_t out_size) {
    static const char *candidates[] = {
        "/CDROM/DOOM/DOOM1.WAD",
        "/CDROM/DOOM/DOOM.WAD",
        "/CDROM/DOOM/DOOM2.WAD",
        "/CDROM/DOOM/FREEDOOM1.WAD",
        "/CDROM/DOOM/FREEDOOM2.WAD",
        "/DOOM/DOOM1.WAD",
        "/DOOM/DOOM.WAD",
        "/DOOM/DOOM2.WAD",
        "/DOOM/FREEDOOM1.WAD",
        "/DOOM/FREEDOOM2.WAD",
        "/DOOM/FREEDM.WAD",
        "/DOOM1.WAD",
        "/DOOM.WAD",
        "/DOOM2.WAD",
        "/FREEDOOM1.WAD",
        "/FREEDOOM2.WAD",
        "/FREEDM.WAD",
    };

    for (uint32_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        int fd = bk_file_open(candidates[i], VFS_O_RDONLY);
        if (fd >= 0) {
            bk_file_close(fd);
            doom_copy_text(out, out_size, candidates[i]);
            return true;
        }
    }

    if (out && out_size) out[0] = '\0';
    return false;
}

static void doom_cleanup(doom_state_t *st) {
    if (!st) return;

    libc_set_exit_handler(NULL);
    doom_host_detach();

    if (st->window) {
        bk_gui_desktop_remove_window(st->desktop, st->window);
        bk_gui_window_destroy_raw(st->window);
        bk_proc_bind_window(NULL);
        st->window = NULL;
    }
    if (st->framebuffer) {
        bk_sys_free(st->framebuffer);
        st->framebuffer = NULL;
    }
    if (g_doom == st) g_doom = NULL;
    bk_sys_free(st);
}

static void doom_exit_handler(int status) {
    kprintf("[doom] exit handler status=%d\n", status);
    if (g_doom) doom_cleanup(g_doom);
}

static void doom_content(gui_window_t *window, gui_surface_t *surface,
                         void *context) {
    doom_state_t *st = (doom_state_t *)context;
    gui_rect_t body;
    int content_x;
    int content_y;

    if (!window || !surface || !st) return;

    body.x = window->bounds.x + 2;
    body.y = window->bounds.y + bk_gui_window_content_top(window);
    body.w = window->bounds.w - 4;
    body.h = window->bounds.h - bk_gui_window_content_top(window) - 2;

    bk_gui_gfx_fill_rect(surface, body, 0x00000000);

    content_x = body.x + (body.w - DOOM_FB_WIDTH) / 2;
    content_y = body.y + (body.h - DOOM_FB_HEIGHT) / 2;
    if (content_x < body.x) content_x = body.x;
    if (content_y < body.y) content_y = body.y;

    if (st->running && st->framebuffer) {
        for (int y = 0; y < DOOM_FB_HEIGHT; y++) {
            int sy = content_y + y;
            if (sy < 0 || sy >= surface->height) continue;
            for (int x = 0; x < DOOM_FB_WIDTH; x++) {
                int sx = content_x + x;
                if (sx < 0 || sx >= surface->width) continue;
                surface->pixels[sy * surface->pitch + sx] =
                    st->framebuffer[y * DOOM_FB_WIDTH + x];
            }
        }
        return;
    }

    bk_gui_font_draw_string(surface, body.x + 18, body.y + 18,
                         "DOOM para BlesKernOS", 0x00FFFFFF, 0, false);
    bk_gui_font_draw_string(surface, body.x + 18, body.y + 40,
                         st->message[0] ? st->message
                                        : "Pon un WAD valido en /DOOM.",
                         0x00E0E0E0, 0, false);
    bk_gui_font_draw_string(surface, body.x + 18, body.y + 58,
                         st->wad_path[0] ? st->wad_path
                                         : "Ejemplos: DOOM1.WAD o FREEDOOM1.WAD",
                         0x00C0C0C0, 0, false);
}

static void doom_main(void *argument) {
    doom_state_t *st = (doom_state_t *)argument;

    if (!st || !st->desktop) {
        doom_cleanup(st);
        bk_proc_exit();
    }

    bk_proc_set_memory_hint(sizeof(*st) +
                         (DOOM_FB_WIDTH * DOOM_FB_HEIGHT * sizeof(uint32_t)));
    st->framebuffer = (uint32_t *)bk_sys_alloc_zero(DOOM_FB_WIDTH * DOOM_FB_HEIGHT *
                                          sizeof(uint32_t));
    if (!st->framebuffer) {
        doom_copy_text(st->message, sizeof(st->message), "Sin RAM para el framebuffer de Doom.");
    }

    st->window = bk_gui_create_window(st->desktop, 14, 18,
                                           DOOM_WINDOW_W, DOOM_WINDOW_H,
                                           "Doom");
    if (st->window) {
        bk_gui_set_window_content(st->window, doom_content, st);
        bk_gui_set_window_min_size(st->window, DOOM_WINDOW_W, DOOM_WINDOW_H);
        st->window->owner_pid = bk_sys_getpid();
        bk_proc_bind_window(st->window);
    }

    if (st->framebuffer && doom_find_wad(st->wad_path, sizeof(st->wad_path))) {
        char *argv[5];

        argv[0] = "doom";
        argv[1] = "-nogui";
        argv[2] = "-iwad";
        argv[3] = st->wad_path;
        argv[4] = NULL;

        kprintf("[doom] iniciando motor con %s\n", st->wad_path);
        doom_copy_text(st->message, sizeof(st->message),
                       "Cargando Doom, espera un momento...");
        if (st->window) st->window->dirty = true;
        doom_host_attach(st->window, st->framebuffer, DOOM_FB_WIDTH, DOOM_FB_HEIGHT);
        libc_set_exit_handler(doom_exit_handler);
        doomgeneric_Create(4, argv);
        kprintf("[doom] engine retornó\n");
        st->running = true;
        doom_copy_text(st->message, sizeof(st->message), "");
    } else if (!st->message[0]) {
        doom_copy_text(st->message, sizeof(st->message),
                       "No encontre un WAD. Copialo a /DOOM y reabre Doom.");
    }

    while (!bk_proc_exit_requested()) {
        if (!st->window || !st->window->listed) break;
        if (st->running) doomgeneric_Tick();
        bk_sys_sleep_ticks(1);
    }

    doom_cleanup(st);
    bk_proc_exit();
}

void doom_open_from_desktop(gui_desktop_t *desktop) {
    doom_state_t *st;

    if (!desktop) return;
    if (g_doom && g_doom->window) {
        bk_gui_desktop_raise_window(desktop, g_doom->window);
        bk_gui_focus_window(desktop, g_doom->window);
        return;
    }
    st = (doom_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    g_doom = st;
    if (bk_proc_spawn_thread("doom", doom_main, st) < 0) {
        doom_cleanup(st);
    }
}

void doom_install(gui_desktop_t *desktop UNUSED) {
}

void bleskernos_program_main(gui_desktop_t *desktop) {
    doom_open_from_desktop(desktop);
}
