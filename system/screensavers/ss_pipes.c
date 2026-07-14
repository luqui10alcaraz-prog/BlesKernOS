/*
 * BlesPipes - TinyGL 3D Pipes screensaver for BlesKernOS
 *
 * Inspired by the classic 90s "3D Pipes" screensaver style.
 * It is implemented as a GUI program overlay, like SSLOGO.
 */

#include "kernel/include/api.h"
#include <math.h>
#include <stdint.h>
#include <TGL/gl.h>
#include "zbuffer.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SS_PIPES_W 320
#define SS_PIPES_H 240
static uint16_t g_pipes_render_w = SS_PIPES_W;
static uint16_t g_pipes_render_h = SS_PIPES_H;
#define SS_PIPES_MAX_SEGMENTS 80
#define SS_PIPES_CELL 1.15f
#define SS_PIPES_RADIUS 0.18f
#define SS_PIPES_GRID_LIMIT 5
#define SS_PIPES_SIDES 12

typedef struct {
    int x;
    int y;
    int z;
    uint8_t dir;
    uint8_t color;
} ss_pipe_segment_t;

typedef struct {
    gui_desktop_t *desktop;
    gui_program_t *program;
    ZBuffer *framebuffer;
    PIXEL *pixels;
    uint16_t render_w;
    uint16_t render_h;
    bool active;
    bool gl_ready;
    uint32_t last_tick;
    uint32_t last_grow_tick;
    uint32_t seed;
    uint32_t frames;
    int cursor_x;
    int cursor_y;
    int cursor_z;
    uint8_t last_dir;
    uint8_t color_index;
    uint8_t segment_count;
    GLfloat rot_x;
    GLfloat rot_y;
    GLfloat rot_z;
    ss_pipe_segment_t segments[SS_PIPES_MAX_SEGMENTS];
} ss_pipes_state_t;

static ss_pipes_state_t *g_ss_pipes;

static const int8_t g_dirs[6][3] = {
    { 1,  0,  0},
    {-1,  0,  0},
    { 0,  1,  0},
    { 0, -1,  0},
    { 0,  0,  1},
    { 0,  0, -1},
};

static const GLfloat g_colors[][3] = {
    {0.10f, 0.65f, 1.00f},
    {0.15f, 1.00f, 0.25f},
    {1.00f, 0.25f, 0.18f},
    {1.00f, 0.85f, 0.18f},
    {0.85f, 0.25f, 1.00f},
    {0.95f, 0.95f, 0.95f},
};

static uint32_t ss_pipes_rand(ss_pipes_state_t *st) {
    st->seed = st->seed * 1664525U + 1013904223U;
    return st->seed;
}

static bool ss_pipes_input_event(const gui_event_t *event) {
    return event &&
           (event->type == GUI_EVENT_MOUSE_MOVE ||
            event->type == GUI_EVENT_MOUSE_DOWN ||
            event->type == GUI_EVENT_MOUSE_UP ||
            event->type == GUI_EVENT_MOUSE_WHEEL ||
            event->type == GUI_EVENT_KEY);
}

static uint32_t ss_pipes_pixel_to_rgb(PIXEL pixel) {
    return ((uint32_t)GET_RED(pixel) << 16) |
           ((uint32_t)GET_GREEN(pixel) << 8) |
           ((uint32_t)GET_BLUE(pixel));
}

static void ss_pipes_configure_resolution(ss_pipes_state_t *st,
                                          const gui_desktop_t *desktop) {
    uint32_t w = SS_PIPES_W;
    uint32_t h = SS_PIPES_H;

    if (!st) return;

    if (desktop && desktop->surface.width && desktop->surface.height) {
        w = desktop->surface.width;
        h = desktop->surface.height;
    } else {
        if (g_pipes_render_w) w = g_pipes_render_w;
        if (g_pipes_render_h) h = g_pipes_render_h;
    }

    /*
     * TinyGL/ZBuffer usa dimensiones enteras chicas en este OS.
     * Si en el futuro pasas de 65535 px no seria realista para este backend.
     */
    if (w > 65535U) w = 65535U;
    if (h > 65535U) h = 65535U;
    if (w < 16U) w = SS_PIPES_W;
    if (h < 16U) h = SS_PIPES_H;

    st->render_w = (uint16_t)w;
    st->render_h = (uint16_t)h;
    g_pipes_render_w = st->render_w;
    g_pipes_render_h = st->render_h;
}

static void ss_pipes_material(uint8_t color) {
    GLfloat diffuse[4];
    GLfloat specular[4] = {0.90f, 0.90f, 0.90f, 1.0f};
    GLfloat shininess = 18.0f;
    const GLfloat *src = g_colors[color % (sizeof(g_colors) / sizeof(g_colors[0]))];

    diffuse[0] = src[0];
    diffuse[1] = src[1];
    diffuse[2] = src[2];
    diffuse[3] = 1.0f;

    glMaterialfv(GL_FRONT, GL_DIFFUSE, diffuse);
    glMaterialfv(GL_FRONT, GL_SPECULAR, specular);
    glMaterialfv(GL_FRONT, GL_SHININESS, &shininess);
    glColor3fv(diffuse);
}

static void ss_pipes_draw_box(GLfloat cx, GLfloat cy, GLfloat cz, GLfloat r) {
    GLfloat x0 = cx - r, x1 = cx + r;
    GLfloat y0 = cy - r, y1 = cy + r;
    GLfloat z0 = cz - r, z1 = cz + r;

    glBegin(GL_QUADS);

    glNormal3f(0.0f, 0.0f, 1.0f);
    glVertex3f(x0, y0, z1); glVertex3f(x1, y0, z1);
    glVertex3f(x1, y1, z1); glVertex3f(x0, y1, z1);

    glNormal3f(0.0f, 0.0f, -1.0f);
    glVertex3f(x1, y0, z0); glVertex3f(x0, y0, z0);
    glVertex3f(x0, y1, z0); glVertex3f(x1, y1, z0);

    glNormal3f(1.0f, 0.0f, 0.0f);
    glVertex3f(x1, y0, z1); glVertex3f(x1, y0, z0);
    glVertex3f(x1, y1, z0); glVertex3f(x1, y1, z1);

    glNormal3f(-1.0f, 0.0f, 0.0f);
    glVertex3f(x0, y0, z0); glVertex3f(x0, y0, z1);
    glVertex3f(x0, y1, z1); glVertex3f(x0, y1, z0);

    glNormal3f(0.0f, 1.0f, 0.0f);
    glVertex3f(x0, y1, z1); glVertex3f(x1, y1, z1);
    glVertex3f(x1, y1, z0); glVertex3f(x0, y1, z0);

    glNormal3f(0.0f, -1.0f, 0.0f);
    glVertex3f(x0, y0, z0); glVertex3f(x1, y0, z0);
    glVertex3f(x1, y0, z1); glVertex3f(x0, y0, z1);

    glEnd();
}

/* Axis: 0=X, 1=Y, 2=Z. Always draws in the positive direction. */
static void ss_pipes_draw_cylinder(int axis, GLfloat x, GLfloat y, GLfloat z,
                                   GLfloat length, GLfloat radius) {
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= SS_PIPES_SIDES; i++) {
        GLfloat a = (GLfloat)i * (GLfloat)(2.0 * M_PI) / (GLfloat)SS_PIPES_SIDES;
        GLfloat c = cos(a);
        GLfloat s = sin(a);

        if (axis == 0) {
            glNormal3f(0.0f, c, s);
            glVertex3f(x,          y + c * radius, z + s * radius);
            glVertex3f(x + length, y + c * radius, z + s * radius);
        } else if (axis == 1) {
            glNormal3f(c, 0.0f, s);
            glVertex3f(x + c * radius, y,          z + s * radius);
            glVertex3f(x + c * radius, y + length, z + s * radius);
        } else {
            glNormal3f(c, s, 0.0f);
            glVertex3f(x + c * radius, y + s * radius, z);
            glVertex3f(x + c * radius, y + s * radius, z + length);
        }
    }
    glEnd();
}

static void ss_pipes_draw_segment(const ss_pipe_segment_t *seg) {
    GLfloat x0 = (GLfloat)seg->x * SS_PIPES_CELL;
    GLfloat y0 = (GLfloat)seg->y * SS_PIPES_CELL;
    GLfloat z0 = (GLfloat)seg->z * SS_PIPES_CELL;
    int dx = g_dirs[seg->dir][0];
    int dy = g_dirs[seg->dir][1];
    int dz = g_dirs[seg->dir][2];

    ss_pipes_material(seg->color);

    if (dx) {
        if (dx < 0) x0 -= SS_PIPES_CELL;
        ss_pipes_draw_cylinder(0, x0, y0, z0, SS_PIPES_CELL, SS_PIPES_RADIUS);
    } else if (dy) {
        if (dy < 0) y0 -= SS_PIPES_CELL;
        ss_pipes_draw_cylinder(1, x0, y0, z0, SS_PIPES_CELL, SS_PIPES_RADIUS);
    } else {
        if (dz < 0) z0 -= SS_PIPES_CELL;
        ss_pipes_draw_cylinder(2, x0, y0, z0, SS_PIPES_CELL, SS_PIPES_RADIUS);
    }

    ss_pipes_draw_box((GLfloat)seg->x * SS_PIPES_CELL,
                      (GLfloat)seg->y * SS_PIPES_CELL,
                      (GLfloat)seg->z * SS_PIPES_CELL,
                      SS_PIPES_RADIUS * 1.35f);
    ss_pipes_draw_box((GLfloat)(seg->x + dx) * SS_PIPES_CELL,
                      (GLfloat)(seg->y + dy) * SS_PIPES_CELL,
                      (GLfloat)(seg->z + dz) * SS_PIPES_CELL,
                      SS_PIPES_RADIUS * 1.35f);
}

static bool ss_pipes_inside(int x, int y, int z) {
    return x >= -SS_PIPES_GRID_LIMIT && x <= SS_PIPES_GRID_LIMIT &&
           y >= -SS_PIPES_GRID_LIMIT && y <= SS_PIPES_GRID_LIMIT &&
           z >= -SS_PIPES_GRID_LIMIT && z <= SS_PIPES_GRID_LIMIT;
}

static uint8_t ss_pipes_opposite(uint8_t dir) {
    return (uint8_t)(dir ^ 1U);
}

static void ss_pipes_push_segment(ss_pipes_state_t *st, uint8_t dir) {
    ss_pipe_segment_t *seg;

    if (st->segment_count >= SS_PIPES_MAX_SEGMENTS) {
        for (uint32_t i = 1; i < SS_PIPES_MAX_SEGMENTS; i++)
            st->segments[i - 1] = st->segments[i];
        st->segment_count = SS_PIPES_MAX_SEGMENTS - 1;
    }

    seg = &st->segments[st->segment_count++];
    seg->x = st->cursor_x;
    seg->y = st->cursor_y;
    seg->z = st->cursor_z;
    seg->dir = dir;
    seg->color = st->color_index;

    st->cursor_x += g_dirs[dir][0];
    st->cursor_y += g_dirs[dir][1];
    st->cursor_z += g_dirs[dir][2];
    st->last_dir = dir;
}

static void ss_pipes_grow(ss_pipes_state_t *st) {
    uint8_t chosen = ss_pipes_opposite(st->last_dir);
    bool found = false;

    for (uint32_t tries = 0; tries < 16; tries++) {
        uint8_t dir = (uint8_t)(ss_pipes_rand(st) % 6U);
        int nx;

        if (st->segment_count && dir == ss_pipes_opposite(st->last_dir))
            continue;

        nx = st->cursor_x + g_dirs[dir][0];
        if (ss_pipes_inside(nx,
                            st->cursor_y + g_dirs[dir][1],
                            st->cursor_z + g_dirs[dir][2])) {
            chosen = dir;
            found = true;
            break;
        }
    }

    if (!found) {
        for (uint8_t dir = 0; dir < 6; dir++) {
            if (ss_pipes_inside(st->cursor_x + g_dirs[dir][0],
                                st->cursor_y + g_dirs[dir][1],
                                st->cursor_z + g_dirs[dir][2])) {
                chosen = dir;
                break;
            }
        }
    }

    if ((ss_pipes_rand(st) & 7U) == 0)
        st->color_index = (uint8_t)((st->color_index + 1U) %
            (sizeof(g_colors) / sizeof(g_colors[0])));

    ss_pipes_push_segment(st, chosen);
}

static bool ss_pipes_init_gl(ss_pipes_state_t *st) {
    GLfloat h;
    GLfloat light_pos[4] = {3.0f, 4.0f, 8.0f, 0.0f};
    GLfloat white[4] = {0.85f, 0.85f, 0.85f, 1.0f};
    GLfloat ambient[4] = {0.16f, 0.16f, 0.20f, 1.0f};

    uint32_t pixel_count;

    if (!st->render_w || !st->render_h) {
        st->render_w = SS_PIPES_W;
        st->render_h = SS_PIPES_H;
    }

    pixel_count = (uint32_t)st->render_w * (uint32_t)st->render_h;

    st->pixels = (PIXEL *)bk_sys_alloc_zero(pixel_count * sizeof(PIXEL));
    if (!st->pixels) return false;

    st->framebuffer = ZB_open(st->render_w, st->render_h,
#if TGL_FEATURE_RENDER_BITS == 32
                              ZB_MODE_RGBA,
#else
                              ZB_MODE_5R6G5B,
#endif
                              0);
    if (!st->framebuffer) return false;

    glInit(st->framebuffer);
    st->gl_ready = true;

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glViewport(0, 0, st->render_w, st->render_h);
    glShadeModel(GL_SMOOTH);

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glLightfv(GL_LIGHT0, GL_POSITION, light_pos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, white);
    glLightfv(GL_LIGHT0, GL_SPECULAR, white);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
    glSetEnableSpecular(GL_TRUE);

    h = (GLfloat)st->render_h / (GLfloat)st->render_w;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -h, h, 4.0, 80.0);

    st->cursor_x = 0;
    st->cursor_y = 0;
    st->cursor_z = 0;
    st->last_dir = 0;
    st->color_index = 0;
    st->rot_x = 25.0f;
    st->rot_y = 0.0f;
    st->rot_z = 0.0f;

    for (uint32_t i = 0; i < 18; i++)
        ss_pipes_grow(st);

    return true;
}

static void ss_pipes_render_frame(ss_pipes_state_t *st) {
    uint32_t now;

    if (!st || !st->gl_ready || !st->framebuffer || !st->pixels) return;

    now = bk_sys_ticks();
    if ((uint32_t)(now - st->last_grow_tick) >= 10U) {
        st->last_grow_tick = now;
        ss_pipes_grow(st);
    }

    st->rot_y += 1.2f;
    st->rot_z += 0.25f;
    if (st->rot_y >= 360.0f) st->rot_y -= 360.0f;
    if (st->rot_z >= 360.0f) st->rot_z -= 360.0f;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -18.0f);
    glRotatef(st->rot_x, 1.0f, 0.0f, 0.0f);
    glRotatef(st->rot_y, 0.0f, 1.0f, 0.0f);
    glRotatef(st->rot_z, 0.0f, 0.0f, 1.0f);

    for (uint32_t i = 0; i < st->segment_count; i++)
        ss_pipes_draw_segment(&st->segments[i]);

    ZB_copyFrameBuffer(st->framebuffer, st->pixels,
                       st->render_w * sizeof(PIXEL));
    st->frames++;
}

static void ss_pipes_blit(gui_surface_t *surface, ss_pipes_state_t *st) {
    uint16_t rw;
    uint16_t rh;
    int x0 = 0;
    int y0 = 0;

    if (!surface || !st || !st->pixels) return;

    rw = st->render_w ? st->render_w : SS_PIPES_W;
    rh = st->render_h ? st->render_h : SS_PIPES_H;

    /*
     * Protector de pantalla puro: sin escalado.
     * Se renderiza al tamaño real de la superficie y se copia 1:1.
     * Si por algun motivo la superficie cambio despues de iniciar, centramos/recortamos.
     */
    if (surface->width > rw) x0 = ((int)surface->width - (int)rw) / 2;
    if (surface->height > rh) y0 = ((int)surface->height - (int)rh) / 2;

    for (int y = 0; y < rh; y++) {
        int sy = y0 + y;
        if (sy < 0 || sy >= (int)surface->height) continue;

        for (int x = 0; x < rw; x++) {
            int sx = x0 + x;
            if (sx < 0 || sx >= (int)surface->width) continue;

            bk_gui_gfx_putpixel(surface, sx, sy,
                ss_pipes_pixel_to_rgb(st->pixels[y * rw + x]));
        }
    }
}

static void ss_pipes_paint(gui_program_t *program, gui_desktop_t *desktop,
                           gui_surface_t *surface) {
    ss_pipes_state_t *st = program ? (ss_pipes_state_t *)program->state : NULL;
    uint32_t now;

    if (!st || !desktop || !surface || !st->active) return;

    now = bk_sys_ticks();
    if ((uint32_t)(now - st->last_tick) >= 3U) {
        st->last_tick = now;
        ss_pipes_render_frame(st);
    }

    bk_gui_gfx_clear(surface, 0x00000000);
    if (st->pixels)
        ss_pipes_blit(surface, st);

    bk_gui_request_paint();
}

static void ss_pipes_cleanup(gui_desktop_t *desktop, ss_pipes_state_t *st) {
    gui_program_t *program;

    if (!st) return;

    program = st->program;
    st->active = false;
    if (g_ss_pipes == st) g_ss_pipes = NULL;

    if (desktop && program) {
        st->program = NULL;
        bk_gui_desktop_unregister_program(desktop, program);
    }

    if (st->gl_ready) {
        if (st->framebuffer) ZB_close(st->framebuffer);
        glClose();
    }

    if (st->pixels) bk_sys_free(st->pixels);

    bk_sys_free(st);
    bk_gui_gfx_invalidate_front();
    bk_gui_request_paint();
}

static bool ss_pipes_event(gui_program_t *program, gui_desktop_t *desktop,
                           const gui_event_t *event) {
    ss_pipes_state_t *st = program ? (ss_pipes_state_t *)program->state : NULL;

    if (!st || !event) return false;
    if (ss_pipes_input_event(event)) {
        ss_pipes_cleanup(desktop, st);
        return true;
    }
    return false;
}

bool ss_pipes_is_active(void);
bool ss_pipes_handle_global_event(gui_desktop_t *desktop,
                                  const gui_event_t *event) {
    if (!ss_pipes_is_active() || !ss_pipes_input_event(event)) return false;
    ss_pipes_cleanup(desktop, g_ss_pipes);
    return true;
}

bool ss_pipes_is_active(void) {
    return g_ss_pipes != NULL && g_ss_pipes->active;
}

void ss_pipes_open_from_desktop(gui_desktop_t *desktop) {
    ss_pipes_state_t *st;

    if (!desktop || ss_pipes_is_active()) return;

    st = (ss_pipes_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;

    st->desktop = desktop;
    st->active = true;
    st->seed = bk_sys_ticks() ^ 0x51F1E50U;
    st->last_tick = bk_sys_ticks();
    st->last_grow_tick = st->last_tick;
    ss_pipes_configure_resolution(st, desktop);

    st->program = bk_gui_desktop_register_program(desktop, "ss_pipes", st,
                                               ss_pipes_paint,
                                               ss_pipes_event,
                                               NULL);
    if (!st->program) {
        bk_sys_free(st);
        return;
    }

    g_ss_pipes = st;

    if (!ss_pipes_init_gl(st)) {
        bk_gui_request_paint();
        return;
    }

    ss_pipes_render_frame(st);
    bk_gui_desktop_invalidate_all(desktop);
    bk_gui_request_paint();
}

void ss_pipes_main(gui_desktop_t *desktop) {
    gui_desktop_t *target = desktop ? desktop : bk_gui_get_desktop();

    if (!target || ss_pipes_is_active()) return;
    ss_pipes_open_from_desktop(target);
    if (!ss_pipes_is_active()) return;

    /* Keep the owner process alive so the compositor can deliver paint upcalls. */
    while (ss_pipes_is_active() && !bk_proc_exit_requested())
        bk_sys_sleep_ticks(2);

    if (bk_proc_exit_requested() && ss_pipes_is_active())
        ss_pipes_cleanup(target, g_ss_pipes);
}

#ifdef SS_PIPES_EXTERNAL_ENTRY
void bleskernos_program_main(gui_desktop_t *desktop) {
    
    if (desktop && desktop->surface.width && desktop->surface.height) {
        g_pipes_render_w = desktop->surface.width;
        g_pipes_render_h = desktop->surface.height;
    } else {
        g_pipes_render_w = SS_PIPES_W;
        g_pipes_render_h = SS_PIPES_H;
    }

ss_pipes_main(desktop);
}
#endif
