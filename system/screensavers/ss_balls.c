/*
 * BlesBalls - TinyGL 3D falling balls screensaver for BlesKernOS
 *
 * Pelotas 3D caen dentro de una caja angosta, se apilan hasta llenar
 * la pantalla y, tras una pausa, la escena se reinicia automaticamente.
 *
 * Mantiene la misma interfaz que ss_pipes:
 *   - programa overlay del escritorio
 *   - salida por teclado o mouse
 *   - entrada externa opcional mediante SS_BALLS_EXTERNAL_ENTRY
 */

#include "kernel/include/api.h"
#include <math.h>
#include <stdint.h>
#include <TGL/gl.h>
#include "zbuffer.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SS_BALLS_W 320
#define SS_BALLS_H 240

#ifndef SS_BALLS_EMBEDDED_PREVIEW
static uint16_t g_balls_render_w = SS_BALLS_W;
static uint16_t g_balls_render_h = SS_BALLS_H;
#endif

/* Geometria deliberadamente baja: estilo retro y menor costo para TinyGL. */
#define SS_BALLS_SLICES 10
#define SS_BALLS_STACKS 6
#define SS_BALLS_MAX 256

/*
 * La proyeccion se adapta al aspect ratio real de la pantalla.
 * El alto del mundo permanece fijo y el ancho de la caja se calcula
 * usando render_w / render_h, evitando margenes laterales.
 */
#define SS_BALLS_VIEW_NEAR        (4.0f)
#define SS_BALLS_VIEW_HALF_HEIGHT (4.35f)
#define SS_BALLS_BOX_MARGIN       (0.18f)
#define SS_BALLS_FLOOR            (-SS_BALLS_VIEW_HALF_HEIGHT + SS_BALLS_BOX_MARGIN)
#define SS_BALLS_TOP              ( SS_BALLS_VIEW_HALF_HEIGHT - SS_BALLS_BOX_MARGIN)
#define SS_BALLS_BACK             (-0.72f)
#define SS_BALLS_FRONT             (0.72f)

#define SS_BALLS_MIN_RADIUS  (0.37f)
#define SS_BALLS_MAX_RADIUS  (0.46f)
#define SS_BALLS_GRAVITY     (0.0105f)
#define SS_BALLS_SPAWN_TICKS 8U
#define SS_BALLS_RESET_TICKS 220U
#define SS_BALLS_FRAME_TICKS 3U
#define SS_BALLS_SOLVER_PASSES 3

typedef struct {
    GLfloat x;
    GLfloat y;
    GLfloat z;
    GLfloat vx;
    GLfloat vy;
    GLfloat vz;
    GLfloat radius;
    uint8_t color;
    uint16_t age;
} ss_ball_t;

typedef struct {
    gui_desktop_t *desktop;
    gui_program_t *program;
    ZBuffer *framebuffer;
    PIXEL *pixels;
    uint16_t render_w;
    uint16_t render_h;
    bool active;
    bool gl_ready;
    bool filled;
    uint32_t last_tick;
    uint32_t last_spawn_tick;
    uint32_t filled_tick;
    uint32_t seed;
    uint32_t frames;
    uint16_t ball_count;
    GLfloat box_left;
    GLfloat box_right;
    uint8_t next_color;
    ss_ball_t balls[SS_BALLS_MAX];
} ss_balls_state_t;

#ifndef SS_BALLS_EMBEDDED_PREVIEW
static ss_balls_state_t *g_ss_balls;
#endif

/* Tabla de esfera unitaria para no recalcular senos/cosenos por pelota. */
static bool g_sphere_ready;
static GLfloat g_sphere_vertices[SS_BALLS_STACKS + 1]
                                  [SS_BALLS_SLICES + 1][3];

static const GLfloat g_ball_colors[][3] = {
    {0.12f, 0.68f, 1.00f},
    {0.18f, 1.00f, 0.35f},
    {1.00f, 0.23f, 0.16f},
    {1.00f, 0.82f, 0.12f},
    {0.82f, 0.24f, 1.00f},
    {1.00f, 0.42f, 0.72f},
    {0.20f, 1.00f, 0.92f},
    {0.95f, 0.95f, 0.95f},
};

static uint32_t ss_balls_rand(ss_balls_state_t *st) {
    st->seed = st->seed * 1664525U + 1013904223U;
    return st->seed;
}

static GLfloat ss_balls_rand01(ss_balls_state_t *st) {
    return (GLfloat)(ss_balls_rand(st) & 0xFFFFU) / 65535.0f;
}

static GLfloat ss_balls_abs(GLfloat value) {
    return value < 0.0f ? -value : value;
}

#ifndef SS_BALLS_EMBEDDED_PREVIEW
static bool ss_balls_input_event(const gui_event_t *event) {
    return event &&
           (event->type == GUI_EVENT_MOUSE_MOVE ||
            event->type == GUI_EVENT_MOUSE_DOWN ||
            event->type == GUI_EVENT_MOUSE_UP ||
            event->type == GUI_EVENT_MOUSE_WHEEL ||
            event->type == GUI_EVENT_KEY);
}
#endif

static uint32_t ss_balls_pixel_to_rgb(PIXEL pixel) {
    return ((uint32_t)GET_RED(pixel) << 16) |
           ((uint32_t)GET_GREEN(pixel) << 8) |
           ((uint32_t)GET_BLUE(pixel));
}

#ifndef SS_BALLS_EMBEDDED_PREVIEW
static void ss_balls_configure_resolution(ss_balls_state_t *st,
                                           const gui_desktop_t *desktop) {
    uint32_t w = SS_BALLS_W;
    uint32_t h = SS_BALLS_H;

    if (!st) return;

    if (desktop && desktop->surface.width && desktop->surface.height) {
        w = desktop->surface.width;
        h = desktop->surface.height;
    } else {
        if (g_balls_render_w) w = g_balls_render_w;
        if (g_balls_render_h) h = g_balls_render_h;
    }

    if (w > 65535U) w = 65535U;
    if (h > 65535U) h = 65535U;
    if (w < 16U) w = SS_BALLS_W;
    if (h < 16U) h = SS_BALLS_H;

    st->render_w = (uint16_t)w;
    st->render_h = (uint16_t)h;
    g_balls_render_w = st->render_w;
    g_balls_render_h = st->render_h;
}
#endif

static void ss_balls_configure_world(ss_balls_state_t *st) {
    GLfloat aspect;
    GLfloat half_width;

    if (!st || !st->render_w || !st->render_h) return;

    aspect = (GLfloat)st->render_w / (GLfloat)st->render_h;
    half_width = SS_BALLS_VIEW_HALF_HEIGHT * aspect - SS_BALLS_BOX_MARGIN;

    /* Evita una caja degenerada en resoluciones verticales extremas. */
    if (half_width < 1.80f)
        half_width = 1.80f;

    st->box_left = -half_width;
    st->box_right = half_width;
}

static void ss_balls_build_sphere(void) {
    if (g_sphere_ready) return;

    for (int stack = 0; stack <= SS_BALLS_STACKS; stack++) {
        GLfloat latitude = -(GLfloat)M_PI * 0.5f +
            (GLfloat)stack * (GLfloat)M_PI / (GLfloat)SS_BALLS_STACKS;
        GLfloat ring_y = (GLfloat)sin(latitude);
        GLfloat ring_r = (GLfloat)cos(latitude);

        for (int slice = 0; slice <= SS_BALLS_SLICES; slice++) {
            GLfloat longitude = (GLfloat)slice * (GLfloat)(2.0 * M_PI) /
                                (GLfloat)SS_BALLS_SLICES;
            GLfloat c = (GLfloat)cos(longitude);
            GLfloat s = (GLfloat)sin(longitude);

            g_sphere_vertices[stack][slice][0] = ring_r * c;
            g_sphere_vertices[stack][slice][1] = ring_y;
            g_sphere_vertices[stack][slice][2] = ring_r * s;
        }
    }

    g_sphere_ready = true;
}

static void ss_balls_material(uint8_t color) {
    GLfloat diffuse[4];
    GLfloat specular[4] = {0.92f, 0.92f, 0.92f, 1.0f};
    GLfloat shininess = 26.0f;
    const GLfloat *src = g_ball_colors[
        color % (sizeof(g_ball_colors) / sizeof(g_ball_colors[0]))];

    diffuse[0] = src[0];
    diffuse[1] = src[1];
    diffuse[2] = src[2];
    diffuse[3] = 1.0f;

    glMaterialfv(GL_FRONT, GL_DIFFUSE, diffuse);
    glMaterialfv(GL_FRONT, GL_SPECULAR, specular);
    glMaterialfv(GL_FRONT, GL_SHININESS, &shininess);
    glColor3fv(diffuse);
}

static void ss_balls_draw_sphere(const ss_ball_t *ball) {
    if (!ball) return;

    ss_balls_material(ball->color);

    for (int stack = 0; stack < SS_BALLS_STACKS; stack++) {
        glBegin(GL_QUAD_STRIP);

        for (int slice = 0; slice <= SS_BALLS_SLICES; slice++) {
            const GLfloat *a = g_sphere_vertices[stack + 1][slice];
            const GLfloat *b = g_sphere_vertices[stack][slice];

            /* Orden antihorario visto desde afuera: necesario con culling. */
            glNormal3f(b[0], b[1], b[2]);
            glVertex3f(ball->x + b[0] * ball->radius,
                       ball->y + b[1] * ball->radius,
                       ball->z + b[2] * ball->radius);

            glNormal3f(a[0], a[1], a[2]);
            glVertex3f(ball->x + a[0] * ball->radius,
                       ball->y + a[1] * ball->radius,
                       ball->z + a[2] * ball->radius);
        }

        glEnd();
    }
}

static void ss_balls_draw_box(const ss_balls_state_t *st) {
    GLfloat floor_diffuse[4] = {0.055f, 0.070f, 0.100f, 1.0f};
    GLfloat wall_diffuse[4] = {0.025f, 0.035f, 0.055f, 1.0f};
    GLfloat no_specular[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    GLfloat zero = 0.0f;

    glMaterialfv(GL_FRONT, GL_SPECULAR, no_specular);
    glMaterialfv(GL_FRONT, GL_SHININESS, &zero);

    /* Fondo oscuro. */
    glMaterialfv(GL_FRONT, GL_DIFFUSE, wall_diffuse);
    glColor3fv(wall_diffuse);
    glBegin(GL_QUADS);
    glNormal3f(0.0f, 0.0f, 1.0f);
    glVertex3f(st->box_left,  SS_BALLS_FLOOR, SS_BALLS_BACK);
    glVertex3f(st->box_right, SS_BALLS_FLOOR, SS_BALLS_BACK);
    glVertex3f(st->box_right, SS_BALLS_TOP,   SS_BALLS_BACK);
    glVertex3f(st->box_left,  SS_BALLS_TOP,   SS_BALLS_BACK);
    glEnd();

    /* Piso. */
    glMaterialfv(GL_FRONT, GL_DIFFUSE, floor_diffuse);
    glColor3fv(floor_diffuse);
    glBegin(GL_QUADS);
    glNormal3f(0.0f, 1.0f, 0.0f);
    glVertex3f(st->box_left,  SS_BALLS_FLOOR, SS_BALLS_FRONT);
    glVertex3f(st->box_right, SS_BALLS_FLOOR, SS_BALLS_FRONT);
    glVertex3f(st->box_right, SS_BALLS_FLOOR, SS_BALLS_BACK);
    glVertex3f(st->box_left,  SS_BALLS_FLOOR, SS_BALLS_BACK);
    glEnd();
}

static void ss_balls_spawn(ss_balls_state_t *st) {
    ss_ball_t *ball;
    GLfloat radius;
    GLfloat span_x;
    GLfloat center_z;

    if (!st || st->ball_count >= SS_BALLS_MAX) return;

    ball = &st->balls[st->ball_count++];
    radius = SS_BALLS_MIN_RADIUS +
        ss_balls_rand01(st) * (SS_BALLS_MAX_RADIUS - SS_BALLS_MIN_RADIUS);
    span_x = (st->box_right - st->box_left) - radius * 2.4f;
    center_z = (SS_BALLS_BACK + SS_BALLS_FRONT) * 0.5f;

    ball->radius = radius;
    ball->x = st->box_left + radius * 1.2f +
              ss_balls_rand01(st) * span_x;
    ball->y = SS_BALLS_TOP + radius +
              ss_balls_rand01(st) * 1.5f;
    ball->z = center_z + (ss_balls_rand01(st) - 0.5f) * 0.36f;

    ball->vx = (ss_balls_rand01(st) - 0.5f) * 0.035f;
    ball->vy = -0.015f - ss_balls_rand01(st) * 0.020f;
    ball->vz = (ss_balls_rand01(st) - 0.5f) * 0.018f;
    {
        uint8_t color_count = (uint8_t)(
            sizeof(g_ball_colors) / sizeof(g_ball_colors[0]));
        uint8_t jump;

        /*
         * Cada pelota usa un color distinto de la anterior.
         * El salto aleatorio evita que la secuencia parezca un arcoiris fijo.
         */
        ball->color = (uint8_t)(st->next_color % color_count);
        jump = (uint8_t)(1U + ss_balls_rand(st) % (color_count - 1U));
        st->next_color = (uint8_t)((ball->color + jump) % color_count);
    }
    ball->age = 0;
}

static void ss_balls_resolve_walls(const ss_balls_state_t *st,
                                    ss_ball_t *ball) {
    GLfloat min_x = st->box_left + ball->radius;
    GLfloat max_x = st->box_right - ball->radius;
    GLfloat min_z = SS_BALLS_BACK + ball->radius;
    GLfloat max_z = SS_BALLS_FRONT - ball->radius;
    GLfloat floor_y = SS_BALLS_FLOOR + ball->radius;

    if (ball->x < min_x) {
        ball->x = min_x;
        if (ball->vx < 0.0f) ball->vx = -ball->vx * 0.48f;
    } else if (ball->x > max_x) {
        ball->x = max_x;
        if (ball->vx > 0.0f) ball->vx = -ball->vx * 0.48f;
    }

    if (ball->z < min_z) {
        ball->z = min_z;
        if (ball->vz < 0.0f) ball->vz = -ball->vz * 0.40f;
    } else if (ball->z > max_z) {
        ball->z = max_z;
        if (ball->vz > 0.0f) ball->vz = -ball->vz * 0.40f;
    }

    if (ball->y < floor_y) {
        ball->y = floor_y;

        if (ball->vy < -0.035f)
            ball->vy = -ball->vy * 0.24f;
        else
            ball->vy = 0.0f;

        ball->vx *= 0.94f;
        ball->vz *= 0.94f;
    }
}

static void ss_balls_resolve_pair(ss_ball_t *a, ss_ball_t *b) {
    GLfloat dx = b->x - a->x;
    GLfloat dy = b->y - a->y;
    GLfloat dz = b->z - a->z;
    GLfloat min_dist = a->radius + b->radius;
    GLfloat dist_sq = dx * dx + dy * dy + dz * dz;
    GLfloat dist;
    GLfloat nx;
    GLfloat ny;
    GLfloat nz;
    GLfloat overlap;
    GLfloat relative_speed;
    GLfloat impulse;

    if (dist_sq >= min_dist * min_dist) return;

    if (dist_sq < 0.000001f) {
        dx = 0.01f;
        dy = 0.0f;
        dz = 0.0f;
        dist_sq = dx * dx;
    }

    dist = (GLfloat)sqrt(dist_sq);
    nx = dx / dist;
    ny = dy / dist;
    nz = dz / dist;
    overlap = min_dist - dist;

    /* Separacion posicional: evita que la pila se hunda sobre si misma. */
    a->x -= nx * overlap * 0.50f;
    a->y -= ny * overlap * 0.50f;
    a->z -= nz * overlap * 0.50f;
    b->x += nx * overlap * 0.50f;
    b->y += ny * overlap * 0.50f;
    b->z += nz * overlap * 0.50f;

    relative_speed = (b->vx - a->vx) * nx +
                     (b->vy - a->vy) * ny +
                     (b->vz - a->vz) * nz;

    if (relative_speed < 0.0f) {
        /* Masas iguales, restitucion baja para formar una pila estable. */
        impulse = -(1.0f + 0.18f) * relative_speed * 0.5f;

        a->vx -= impulse * nx;
        a->vy -= impulse * ny;
        a->vz -= impulse * nz;
        b->vx += impulse * nx;
        b->vy += impulse * ny;
        b->vz += impulse * nz;
    }
}

static void ss_balls_physics_step(ss_balls_state_t *st) {
    if (!st) return;

    for (uint32_t i = 0; i < st->ball_count; i++) {
        ss_ball_t *ball = &st->balls[i];

        ball->vy -= SS_BALLS_GRAVITY;
        ball->x += ball->vx;
        ball->y += ball->vy;
        ball->z += ball->vz;

        ball->vx *= 0.997f;
        ball->vz *= 0.997f;
        if (ball->age < 65535U) ball->age++;

        ss_balls_resolve_walls(st, ball);
    }

    for (int pass = 0; pass < SS_BALLS_SOLVER_PASSES; pass++) {
        for (uint32_t i = 0; i < st->ball_count; i++) {
            for (uint32_t j = i + 1; j < st->ball_count; j++)
                ss_balls_resolve_pair(&st->balls[i], &st->balls[j]);
        }

        for (uint32_t i = 0; i < st->ball_count; i++)
            ss_balls_resolve_walls(st, &st->balls[i]);
    }

    /* Amortiguacion adicional para pelotas casi quietas sobre la pila. */
    for (uint32_t i = 0; i < st->ball_count; i++) {
        ss_ball_t *ball = &st->balls[i];

        if (ss_balls_abs(ball->vy) < 0.004f)
            ball->vy = 0.0f;
        if (ss_balls_abs(ball->vx) < 0.0015f)
            ball->vx = 0.0f;
        if (ss_balls_abs(ball->vz) < 0.0015f)
            ball->vz = 0.0f;
    }
}

static bool ss_balls_pile_reached_top(const ss_balls_state_t *st) {
    uint32_t high_and_slow = 0;

    if (!st || st->ball_count < 60U) return false;

    for (uint32_t i = 0; i < st->ball_count; i++) {
        const ss_ball_t *ball = &st->balls[i];

        if (ball->age > 80U &&
            ball->y + ball->radius > SS_BALLS_TOP - 0.40f &&
            ss_balls_abs(ball->vy) < 0.055f) {
            high_and_slow++;
        }
    }

    return high_and_slow >= 2U;
}

static void ss_balls_reset_scene(ss_balls_state_t *st, uint32_t now) {
    if (!st) return;

    st->ball_count = 0;
    st->filled = false;
    st->filled_tick = 0;
    st->last_spawn_tick = now - SS_BALLS_SPAWN_TICKS;
    st->seed ^= now + 0x9E3779B9U;
    st->next_color = (uint8_t)(ss_balls_rand(st) %
        (sizeof(g_ball_colors) / sizeof(g_ball_colors[0])));
}

static bool ss_balls_init_gl(ss_balls_state_t *st) {
    GLfloat aspect;
    GLfloat light_pos[4] = {-3.5f, 6.5f, 8.0f, 0.0f};
    GLfloat white[4] = {0.90f, 0.90f, 0.90f, 1.0f};
    GLfloat ambient[4] = {0.14f, 0.15f, 0.20f, 1.0f};
    uint32_t pixel_count;

    if (!st->render_w || !st->render_h) {
        st->render_w = SS_BALLS_W;
        st->render_h = SS_BALLS_H;
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

    ss_balls_build_sphere();

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

    aspect = (GLfloat)st->render_w / (GLfloat)st->render_h;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-aspect, aspect, -1.0f, 1.0f,
              SS_BALLS_VIEW_NEAR, 80.0f);

    ss_balls_reset_scene(st, bk_sys_ticks());
    return true;
}

static void ss_balls_render_frame(ss_balls_state_t *st) {
    uint32_t now;

    if (!st || !st->gl_ready || !st->framebuffer || !st->pixels) return;

    now = bk_sys_ticks();

    if (!st->filled && st->ball_count < SS_BALLS_MAX &&
        (uint32_t)(now - st->last_spawn_tick) >= SS_BALLS_SPAWN_TICKS) {
        st->last_spawn_tick = now;
        ss_balls_spawn(st);
    }

    ss_balls_physics_step(st);

    if (!st->filled) {
        bool max_scene_settled =
            st->ball_count >= SS_BALLS_MAX &&
            st->balls[st->ball_count - 1U].age > 160U;

        if (max_scene_settled || ss_balls_pile_reached_top(st)) {
            st->filled = true;
            st->filled_tick = now;
        }
    }

    if (st->filled &&
        (uint32_t)(now - st->filled_tick) >= SS_BALLS_RESET_TICKS) {
        ss_balls_reset_scene(st, now);
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f,
                 -(SS_BALLS_VIEW_HALF_HEIGHT * SS_BALLS_VIEW_NEAR));

    ss_balls_draw_box(st);

    for (uint32_t i = 0; i < st->ball_count; i++)
        ss_balls_draw_sphere(&st->balls[i]);

    ZB_copyFrameBuffer(st->framebuffer, st->pixels,
                       st->render_w * sizeof(PIXEL));
    st->frames++;
}

#ifdef SS_BALLS_EMBEDDED_PREVIEW
void ss_balls_preview_draw(gui_surface_t *surface, gui_rect_t screen) {
    static ss_balls_state_t *preview;
    uint32_t now;

    if (!surface || screen.w < 16 || screen.h < 16) return;

    if (preview && (preview->render_w != (uint16_t)screen.w ||
                    preview->render_h != (uint16_t)screen.h)) {
        if (preview->gl_ready) {
            if (preview->framebuffer) ZB_close(preview->framebuffer);
            glClose();
        }
        if (preview->pixels) bk_sys_free(preview->pixels);
        bk_sys_free(preview);
        preview = NULL;
    }

    if (!preview) {
        preview = (ss_balls_state_t *)bk_sys_alloc_zero(sizeof(*preview));
        if (!preview) return;
        preview->active = true;
        preview->seed = bk_sys_ticks() ^ 0xBA115EEDU;
        preview->last_tick = bk_sys_ticks();
        preview->last_spawn_tick = preview->last_tick;
        preview->render_w = (uint16_t)screen.w;
        preview->render_h = (uint16_t)screen.h;
        ss_balls_configure_world(preview);
        if (!ss_balls_init_gl(preview)) {
            if (preview->framebuffer) ZB_close(preview->framebuffer);
            if (preview->pixels) bk_sys_free(preview->pixels);
            bk_sys_free(preview);
            preview = NULL;
            bk_gui_gfx_fill_rect(surface, screen, 0x00000000);
            return;
        }
        ss_balls_render_frame(preview);
    }

    now = bk_sys_ticks();
    if ((uint32_t)(now - preview->last_tick) >= SS_BALLS_FRAME_TICKS) {
        preview->last_tick = now;
        ss_balls_render_frame(preview);
    }

    if (!preview->pixels) return;
    for (uint16_t y = 0; y < preview->render_h; y++) {
        for (uint16_t x = 0; x < preview->render_w; x++) {
            bk_gui_gfx_putpixel(surface, screen.x + x, screen.y + y,
                ss_balls_pixel_to_rgb(
                    preview->pixels[(uint32_t)y * preview->render_w + x]));
        }
    }
}
#endif

#ifndef SS_BALLS_EMBEDDED_PREVIEW
static void ss_balls_blit(gui_surface_t *surface, ss_balls_state_t *st) {
    uint16_t rw;
    uint16_t rh;
    int x0 = 0;
    int y0 = 0;

    if (!surface || !st || !st->pixels) return;

    rw = st->render_w ? st->render_w : SS_BALLS_W;
    rh = st->render_h ? st->render_h : SS_BALLS_H;

    if (surface->width > rw) x0 = ((int)surface->width - (int)rw) / 2;
    if (surface->height > rh) y0 = ((int)surface->height - (int)rh) / 2;

    for (int y = 0; y < rh; y++) {
        int sy = y0 + y;
        if (sy < 0 || sy >= (int)surface->height) continue;

        for (int x = 0; x < rw; x++) {
            int sx = x0 + x;
            if (sx < 0 || sx >= (int)surface->width) continue;

            bk_gui_gfx_putpixel(surface, sx, sy,
                ss_balls_pixel_to_rgb(st->pixels[y * rw + x]));
        }
    }
}

static void ss_balls_paint(gui_program_t *program, gui_desktop_t *desktop,
                           gui_surface_t *surface) {
    ss_balls_state_t *st = program ? (ss_balls_state_t *)program->state : NULL;
    uint32_t now;

    if (!st || !desktop || !surface || !st->active) return;

    now = bk_sys_ticks();
    if ((uint32_t)(now - st->last_tick) >= SS_BALLS_FRAME_TICKS) {
        st->last_tick = now;
        ss_balls_render_frame(st);
    }

    bk_gui_gfx_clear(surface, 0x00000000);
    if (st->pixels)
        ss_balls_blit(surface, st);

    bk_gui_request_paint();
}

static void ss_balls_cleanup(gui_desktop_t *desktop, ss_balls_state_t *st) {
    gui_program_t *program;

    if (!st) return;

    program = st->program;
    st->active = false;
    if (g_ss_balls == st) g_ss_balls = NULL;

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

static bool ss_balls_event(gui_program_t *program, gui_desktop_t *desktop,
                           const gui_event_t *event) {
    ss_balls_state_t *st = program ? (ss_balls_state_t *)program->state : NULL;

    if (!st || !event) return false;
    if (ss_balls_input_event(event)) {
        ss_balls_cleanup(desktop, st);
        return true;
    }
    return false;
}

bool ss_balls_is_active(void);

bool ss_balls_handle_global_event(gui_desktop_t *desktop,
                                  const gui_event_t *event) {
    if (!ss_balls_is_active() || !ss_balls_input_event(event)) return false;
    ss_balls_cleanup(desktop, g_ss_balls);
    return true;
}

bool ss_balls_is_active(void) {
    return g_ss_balls != NULL && g_ss_balls->active;
}

void ss_balls_open_from_desktop(gui_desktop_t *desktop) {
    ss_balls_state_t *st;

    if (!desktop || ss_balls_is_active()) return;

    st = (ss_balls_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;

    st->desktop = desktop;
    st->active = true;
    st->seed = bk_sys_ticks() ^ 0xBA115EEDU;
    st->last_tick = bk_sys_ticks();
    st->last_spawn_tick = st->last_tick;
    ss_balls_configure_resolution(st, desktop);
    ss_balls_configure_world(st);

    st->program = bk_gui_desktop_register_program(desktop, "ss_balls", st,
                                               ss_balls_paint,
                                               ss_balls_event,
                                               NULL);
    if (!st->program) {
        bk_sys_free(st);
        return;
    }

    g_ss_balls = st;

    if (!ss_balls_init_gl(st)) {
        ss_balls_cleanup(desktop, st);
        return;
    }

    ss_balls_render_frame(st);
    bk_gui_desktop_invalidate_all(desktop);
    bk_gui_request_paint();
}

void ss_balls_main(gui_desktop_t *desktop) {
    gui_desktop_t *target = desktop ? desktop : bk_gui_get_desktop();

    if (!target || ss_balls_is_active()) return;
    ss_balls_open_from_desktop(target);
    if (!ss_balls_is_active()) return;

    /* Keep the owner process alive so the compositor can deliver paint upcalls. */
    while (ss_balls_is_active() && !bk_proc_exit_requested())
        bk_sys_sleep_ticks(2);

    if (bk_proc_exit_requested() && ss_balls_is_active())
        ss_balls_cleanup(target, g_ss_balls);
}

#ifdef SS_BALLS_EXTERNAL_ENTRY
void bleskernos_program_main(gui_desktop_t *desktop) {
    if (desktop && desktop->surface.width && desktop->surface.height) {
        g_balls_render_w = desktop->surface.width;
        g_balls_render_h = desktop->surface.height;
    } else {
        g_balls_render_w = SS_BALLS_W;
        g_balls_render_h = SS_BALLS_H;
    }

    ss_balls_main(desktop);
}
#endif
#endif
