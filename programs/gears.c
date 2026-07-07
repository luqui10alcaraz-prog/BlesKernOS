/*
 * BlesGears - TinyGL gears demo for BlesKernOS
 *
 * Based on TinyGL examples/raw/gears.c, adapted to a BlesKernOS GUI window.
 * This first port is intended as an internal app linked into the kernel build.
 */

#include "programs.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/pit.h"
#include "../kernel/include/task.h"
#include <math.h>
#include <stdint.h>
#include <TGL/gl.h>
#include "zbuffer.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define GEARS_W 320
#define GEARS_H 240
#define GEARS_WINDOW_W 360
#define GEARS_WINDOW_H 300

static int override_drawmodes = 0;
static GLubyte stipplepattern[128] = {
    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55, 0xAA, 0xAA, 0xAA,
    0xAA, 0x55, 0x55, 0x55, 0x55, 0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55,
    0x55, 0x55, 0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,

    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55, 0xAA, 0xAA, 0xAA,
    0xAA, 0x55, 0x55, 0x55, 0x55, 0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55,
    0x55, 0x55, 0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,

    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55, 0xAA, 0xAA, 0xAA,
    0xAA, 0x55, 0x55, 0x55, 0x55, 0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55,
    0x55, 0x55, 0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,

    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55, 0xAA, 0xAA, 0xAA,
    0xAA, 0x55, 0x55, 0x55, 0x55, 0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55,
    0x55, 0x55, 0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,
};

/*
 * Draw a gear wheel.  You'll probably want to call this function when
 * building a display list since we do a lot of trig here.
 *
 * Input:  inner_radius - radius of hole at center
 *         outer_radius - radius at center of teeth
 *         width - width of gear
 *         teeth - number of teeth
 *         tooth_depth - depth of tooth
 */
static void gear(GLfloat inner_radius,
                 GLfloat outer_radius,
                 GLfloat width,
                 GLint teeth,
                 GLfloat tooth_depth)
{
    GLfloat r0, r1, r2;
    GLfloat angle, da;
    GLfloat u, v, len;

    r0 = inner_radius;
    r1 = outer_radius - tooth_depth / 2.0;
    r2 = outer_radius + tooth_depth / 2.0;

    da = 2.0 * M_PI / teeth / 4.0;

    glNormal3f(0.0, 0.0, 1.0);

    /* draw front face */
    if (override_drawmodes == 1)
        glBegin(GL_LINES);
    else if (override_drawmodes == 2)
        glBegin(GL_POINTS);
    else
        glBegin(GL_QUAD_STRIP);
    for (GLint i = 0; i <= teeth; i++) {
        angle = i * 2.0 * M_PI / teeth;
        glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
        glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
        glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
        glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
                   width * 0.5);
    }
    glEnd();

    /* draw front sides of teeth */
    if (override_drawmodes == 1)
        glBegin(GL_LINES);
    else if (override_drawmodes == 2)
        glBegin(GL_POINTS);
    else
        glBegin(GL_QUADS);
    da = 2.0 * M_PI / teeth / 4.0;
    for (GLint i = 0; i < teeth; i++) {
        angle = i * 2.0 * M_PI / teeth;

        glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
        glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), width * 0.5);
        glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da),
                   width * 0.5);
        glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
                   width * 0.5);
    }
    glEnd();

    glNormal3f(0.0, 0.0, -1.0);

    /* draw back face */
    if (override_drawmodes == 1)
        glBegin(GL_LINES);
    else if (override_drawmodes == 2)
        glBegin(GL_POINTS);
    else
        glBegin(GL_QUAD_STRIP);
    for (GLint i = 0; i <= teeth; i++) {
        angle = i * 2.0 * M_PI / teeth;
        glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
        glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
        glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
                   -width * 0.5);
        glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
    }
    glEnd();

    /* draw back sides of teeth */
    if (override_drawmodes == 1)
        glBegin(GL_LINES);
    else if (override_drawmodes == 2)
        glBegin(GL_POINTS);
    else
        glBegin(GL_QUADS);
    da = 2.0 * M_PI / teeth / 4.0;
    for (GLint i = 0; i < teeth; i++) {
        angle = i * 2.0 * M_PI / teeth;

        glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
                   -width * 0.5);
        glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da),
                   -width * 0.5);
        glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), -width * 0.5);
        glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
    }
    glEnd();

    /* draw outward faces of teeth */
    if (override_drawmodes == 1)
        glBegin(GL_LINES);
    else if (override_drawmodes == 2)
        glBegin(GL_POINTS);
    else
        glBegin(GL_QUAD_STRIP);
    for (GLint i = 0; i < teeth; i++) {
        angle = i * 2.0 * M_PI / teeth;

        glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
        glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
        u = r2 * cos(angle + da) - r1 * cos(angle);
        v = r2 * sin(angle + da) - r1 * sin(angle);
        len = sqrt(u * u + v * v);
        u /= len;
        v /= len;
        glNormal3f(v, -u, 0.0);
        glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), width * 0.5);
        glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), -width * 0.5);
        glNormal3f(cos(angle), sin(angle), 0.0);
        glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da),
                   width * 0.5);
        glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da),
                   -width * 0.5);
        u = r1 * cos(angle + 3 * da) - r2 * cos(angle + 2 * da);
        v = r1 * sin(angle + 3 * da) - r2 * sin(angle + 2 * da);
        glNormal3f(v, -u, 0.0);
        glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
                   width * 0.5);
        glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
                   -width * 0.5);
        glNormal3f(cos(angle), sin(angle), 0.0);
    }

    glVertex3f(r1 * cos(0), r1 * sin(0), width * 0.5);
    glVertex3f(r1 * cos(0), r1 * sin(0), -width * 0.5);

    glEnd();

    /* draw inside radius cylinder */
    if (override_drawmodes == 1)
        glBegin(GL_LINES);
    else if (override_drawmodes == 2)
        glBegin(GL_POINTS);
    else
        glBegin(GL_QUAD_STRIP);
    for (GLint i = 0; i <= teeth; i++) {
        angle = i * 2.0 * M_PI / teeth;
        glNormal3f(-cos(angle), -sin(angle), 0.0);
        glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
        glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
    }
    glEnd();
}

static GLfloat view_rotx = 20.0, view_roty = 30.0;
static GLint gear1, gear2, gear3;
static GLfloat angle = 0.0;

static void gears_draw_scene(void)
{
    angle += 2.0;
    glPushMatrix();
    glRotatef(view_rotx, 1.0, 0.0, 0.0);
    glRotatef(view_roty, 0.0, 1.0, 0.0);

    glPushMatrix();
    glTranslatef(-3.0, -2.0, 0.0);
    glRotatef(angle, 0.0, 0.0, 1.0);
    glCallList(gear1);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(3.1, -2.0, 0.0);
    glRotatef(-2.0 * angle - 9.0, 0.0, 0.0, 1.0);
    glCallList(gear2);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(-3.1, 4.2, 0.0);
    glRotatef(-2.0 * angle - 25.0, 0.0, 0.0, 1.0);
    glCallList(gear3);
    glPopMatrix();

    glPopMatrix();
}

static void gears_init_scene(void)
{
    static GLfloat pos[4] = {5, 5, 10, 0.0};  // Light at infinity.

    static GLfloat red[4] = {1.0, 0.0, 0.0, 0.0};
    static GLfloat green[4] = {0.0, 1.0, 0.0, 0.0};
    static GLfloat blue[4] = {0.0, 0.0, 1.0, 0.0};
    static GLfloat white[4] = {1.0, 1.0, 1.0, 0.0};
    static GLfloat shininess = 5;
    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, white);
    glLightfv(GL_LIGHT0, GL_SPECULAR, white);
    glEnable(GL_CULL_FACE);

    glEnable(GL_LIGHT0);

    glEnable(GL_POLYGON_STIPPLE);
    glPolygonStipple(stipplepattern);
    glPointSize(10.0f);
    glTextSize(GL_TEXT_SIZE8x8);

    /* make the gears */
    gear1 = glGenLists(1);
    glNewList(gear1, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, blue);
    glMaterialfv(GL_FRONT, GL_SPECULAR, white);
    glMaterialfv(GL_FRONT, GL_SHININESS, &shininess);
    glColor3fv(blue);
    gear(1.0, 4.0, 1.0, 20, 0.7);  // The largest gear.
    glEndList();

    gear2 = glGenLists(1);
    glNewList(gear2, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, red);
    glMaterialfv(GL_FRONT, GL_SPECULAR, white);
    glColor3fv(red);
    /* The small gear with the smaller hole, to the right. */
    gear(0.5, 2.0, 2.0, 10, 0.7);
    glEndList();

    gear3 = glGenLists(1);
    glNewList(gear3, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, green);
    glMaterialfv(GL_FRONT, GL_SPECULAR, white);
    glColor3fv(green);
    /* The small gear above with the large hole. */
    gear(1.3, 2.0, 0.5, 10, 0.7);
    glEndList();
}

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    ZBuffer *framebuffer;
    PIXEL *pixels;
    bool gl_ready;
    uint32_t last_tick;
    uint32_t frames;
} gears_state_t;

static gears_state_t *g_gears;

static uint32_t gears_pixel_to_rgb(PIXEL pixel)
{
    return ((uint32_t)GET_RED(pixel) << 16) |
           ((uint32_t)GET_GREEN(pixel) << 8) |
           ((uint32_t)GET_BLUE(pixel));
}

static bool gears_init_gl(gears_state_t *st)
{
    GLfloat h;

    if (!st) return false;

    st->pixels = (PIXEL *)kzalloc((uint32_t)(GEARS_W * GEARS_H * sizeof(PIXEL)));
    if (!st->pixels) return false;

    st->framebuffer = ZB_open(GEARS_W, GEARS_H,
#if TGL_FEATURE_RENDER_BITS == 32
                              ZB_MODE_RGBA,
#else
                              ZB_MODE_5R6G5B,
#endif
                              0);
    if (!st->framebuffer) return false;

    glInit(st->framebuffer);
    st->gl_ready = true;

    angle = 0.0f;
    view_rotx = 20.0f;
    view_roty = 30.0f;
    override_drawmodes = 0;

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glViewport(0, 0, GEARS_W, GEARS_H);
    glShadeModel(GL_SMOOTH);

    glEnable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glBlendEquation(GL_FUNC_ADD);

    h = (GLfloat)GEARS_H / (GLfloat)GEARS_W;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -h, h, 5.0, 60.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0, 0.0, -45.0);

    gears_init_scene();
    glSetEnableSpecular(GL_TRUE);

    return true;
}

static void gears_render_frame(gears_state_t *st)
{
    if (!st || !st->gl_ready || !st->framebuffer || !st->pixels) return;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gears_draw_scene();
    /*
     * TinyGL's built-in text was previously using GL_TEXT_SIZE24x24,
     * which looked huge in the 320x240 render area. Keep it small.
     */
    glTextSize(GL_TEXT_SIZE8x8);
    glDrawText((GLubyte *)"BlesGears / TinyGL", 4, 4, 0xFFFFFF);
    ZB_copyFrameBuffer(st->framebuffer, st->pixels, GEARS_W * sizeof(PIXEL));
    st->frames++;
}

static void gears_content(gui_window_t *window, gui_surface_t *surface,
                          void *context)
{
    gears_state_t *st = (gears_state_t *)context;
    int content_top;
    int x0;
    int y0;
    gui_rect_t clip;

    if (!st || !window || !surface || !window->visible) return;

    content_top = gui_window_content_top(window);
    x0 = window->bounds.x + ((window->bounds.w - GEARS_W) / 2);
    y0 = window->bounds.y + content_top + 8;
    if (x0 < window->bounds.x + GUI_BORDER_SIZE) x0 = window->bounds.x + GUI_BORDER_SIZE;
    if (y0 < window->bounds.y + content_top) y0 = window->bounds.y + content_top;

    clip = (gui_rect_t){window->bounds.x + GUI_BORDER_SIZE,
                        window->bounds.y + content_top,
                        window->bounds.w - (GUI_BORDER_SIZE * 2),
                        window->bounds.h - content_top - GUI_BORDER_SIZE};

    gui_gfx_fill_rect(surface, clip, 0x00000000);

    if (!st->pixels) {
        gui_font_draw_string_clipped(surface, clip.x + 8, clip.y + 8,
                                     "TinyGL init failed", 0x00FFFFFF, clip);
        return;
    }

    for (int y = 0; y < GEARS_H; y++) {
        int sy = y0 + y;
        if (sy < clip.y || sy >= clip.y + clip.h) continue;
        for (int x = 0; x < GEARS_W; x++) {
            int sx = x0 + x;
            if (sx < clip.x || sx >= clip.x + clip.w) continue;
            gui_gfx_putpixel(surface, sx, sy,
                             gears_pixel_to_rgb(st->pixels[y * GEARS_W + x]));
        }
    }
}

static bool gears_event(gui_window_t *window, const gui_event_t *event,
                        void *context)
{
    gears_state_t *st = (gears_state_t *)context;

    (void)window;
    if (!st || !event || event->type != GUI_EVENT_KEY) return false;

    if (event->key == 'a' || event->key == 'A') view_roty -= 5.0f;
    else if (event->key == 'd' || event->key == 'D') view_roty += 5.0f;
    else if (event->key == 'w' || event->key == 'W') view_rotx -= 5.0f;
    else if (event->key == 's' || event->key == 'S') view_rotx += 5.0f;
    else if (event->key == '1') override_drawmodes = 0;
    else if (event->key == '2') override_drawmodes = 1;
    else if (event->key == '3') override_drawmodes = 2;
    else return false;

    gears_render_frame(st);
    if (st->window) st->window->dirty = true;
    return true;
}

static void gears_cleanup(gears_state_t *st)
{
    if (!st) return;

    if (st->gl_ready) {
        if (gear1) glDeleteList(gear1);
        if (gear2) glDeleteList(gear2);
        if (gear3) glDeleteList(gear3);
        if (st->framebuffer) ZB_close(st->framebuffer);
        glClose();
    }

    if (st->pixels) kfree(st->pixels);

    if (st->window) {
        gui_desktop_remove_window(st->desktop, st->window);
        gui_window_destroy(st->window);
        task_bind_window(NULL);
    }

    if (g_gears == st) g_gears = NULL;
    kfree(st);
}

bool gears_get_runtime_info(program_runtime_info_t *info)
{
    if (!info || !g_gears) return false;
    info->window = g_gears->window;
    info->memory_bytes = (uint32_t)sizeof(*g_gears);
    info->memory_bytes += (uint32_t)(GEARS_W * GEARS_H * sizeof(PIXEL));
    info->memory_bytes += (uint32_t)(GEARS_W * GEARS_H * sizeof(GLushort));
    return true;
}

static void gears_main(void *argument)
{
    gears_state_t *st = (gears_state_t *)argument;

    if (!st || !st->desktop) {
        gears_cleanup(st);
        task_exit();
    }

    task_set_memory_hint((uint32_t)sizeof(*st) +
                         (uint32_t)(GEARS_W * GEARS_H * sizeof(PIXEL)) +
                         (uint32_t)(GEARS_W * GEARS_H * sizeof(GLushort)));

    st->window = gui_desktop_create_window(st->desktop, 120, 45,
                                           GEARS_WINDOW_W, GEARS_WINDOW_H,
                                           "BlesGears TinyGL");
    if (!st->window) {
        gears_cleanup(st);
        task_exit();
    }

    gui_window_set_min_size(st->window, GEARS_WINDOW_W, GEARS_WINDOW_H);
    gui_window_set_content(st->window, gears_content, st);
    gui_window_set_event_handler(st->window, gears_event, st);
    st->window->owner_pid = task_current_pid();
    task_bind_window(st->window);

    if (!gears_init_gl(st)) {
        st->window->dirty = true;
    } else {
        gears_render_frame(st);
        st->window->dirty = true;
    }

    st->last_tick = pit_get_ticks();

    while (!task_exit_requested()) {
        uint32_t now;
        if (!st->window || !st->window->listed) break;

        now = pit_get_ticks();
        if ((uint32_t)(now - st->last_tick) >= 2U) {
            st->last_tick = now;
            gears_render_frame(st);
            if (st->window) st->window->dirty = true;
        }
        task_sleep(1);
    }

    gears_cleanup(st);
    task_exit();
}

void gears_open_from_desktop(gui_desktop_t *desktop)
{
    gears_state_t *st;

    if (!desktop) return;

    if (g_gears && g_gears->window) {
        gui_window_restore(g_gears->window);
        gui_desktop_raise_window(desktop, g_gears->window);
        gui_desktop_focus_window(desktop, g_gears->window);
        return;
    }

    st = (gears_state_t *)kzalloc(sizeof(*st));
    if (!st) return;

    st->desktop = desktop;
    g_gears = st;

    if (task_create("gears", gears_main, st) < 0) {
        gears_cleanup(st);
    }
}

void gears_install(gui_desktop_t *desktop)
{
    (void)desktop;
}