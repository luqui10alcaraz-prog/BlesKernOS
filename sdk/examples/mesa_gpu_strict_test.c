#include <bleskernos_mesa.h>

/*
 * GPU-only Mesa 3.5 stress test.
 *
 * Return values:
 *   0  = all requested tests completed with no fallback or readback
 *  -1  = invalid arguments
 *  -2  = context creation failed
 *  -3  = no GFX3D backend is available
 *  -4  = strict renderer reported a fallback/driver failure
 *  -5  = GPU presentation failed
 *  -6  = resize failed
 *  -7  = unexpected fallback/readback/statistics result
 *  -8  = repeated context creation/destruction failed
 *
 * Passing frame_count=0 runs 10000 frames. Passing churn_count=0 runs 64
 * context create/destroy cycles. The test deliberately uses the viewport
 * subset intended for 3D Plus: depth, eight CPU-side fixed-function lights,
 * one mipmapped texture, scissor, linear fog, filled and wireframe geometry,
 * normal alpha and additive blending, resize and GPU-only presentation.
 */

typedef struct mesa_gpu_strict_report {
    bk_mesa_gpu_stats_t stats;
    bk_mesa_fallback_t failure;
    uint32_t frames_requested;
    uint32_t frames_completed;
    uint32_t churn_requested;
    uint32_t churn_completed;
} mesa_gpu_strict_report_t;

static const unsigned char texture_4x4[4U * 4U * 4U] = {
    255,  32,  32,255, 255, 192,  32,255,  32, 192,255,255,  32,  32,255,255,
    255, 192,  32,255, 255, 255, 255,255,  32,  32,  32,255,  32, 192,255,255,
     32, 192,255,255,  32,  32,  32,255, 255,255,255,255, 255, 192, 32,255,
     32,  32,255,255,  32, 192,255,255, 255,192, 32,255, 255,  32, 32,255
};

static const unsigned char texture_2x2[2U * 2U * 4U] = {
    255,128, 32,255,  32,128,255,255,
     32,255,128,255, 255, 32,128,255
};

static const unsigned char texture_1x1[4] = { 160, 160, 160, 255 };

static void report_clear(mesa_gpu_strict_report_t *report)
{
    uint32_t *words;
    uint32_t count;
    uint32_t i;
    if (!report) return;
    words = (uint32_t *)report;
    count = (uint32_t)(sizeof(*report) / sizeof(uint32_t));
    for (i = 0U; i < count; ++i) words[i] = 0U;
}

static void setup_texture(void)
{
    GLuint texture = 0U;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, texture_4x4);
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 2, 2, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, texture_2x2);
    glTexImage2D(GL_TEXTURE_2D, 2, GL_RGBA, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, texture_1x1);
}

static void setup_lights(void)
{
    static const GLfloat ambient[4]  = {0.015f, 0.015f, 0.020f, 1.0f};
    static const GLfloat diffuse[4]  = {0.155f, 0.145f, 0.135f, 1.0f};
    static const GLfloat specular[4] = {0.050f, 0.050f, 0.050f, 1.0f};
    GLfloat position[4];
    GLuint i;

    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    for (i = 0U; i < 8U; ++i) {
        position[0] = (i & 1U) ? 2.0f : -2.0f;
        position[1] = (i & 2U) ? 2.0f : -2.0f;
        position[2] = (i & 4U) ? 2.0f : -2.0f;
        position[3] = 0.0f;
        glLightfv(GL_LIGHT0 + i, GL_AMBIENT, ambient);
        glLightfv(GL_LIGHT0 + i, GL_DIFFUSE, diffuse);
        glLightfv(GL_LIGHT0 + i, GL_SPECULAR, specular);
        glLightfv(GL_LIGHT0 + i, GL_POSITION, position);
        glEnable(GL_LIGHT0 + i);
    }
}

static void draw_face(GLfloat nx, GLfloat ny, GLfloat nz,
                      GLfloat ax, GLfloat ay, GLfloat az,
                      GLfloat bx, GLfloat by, GLfloat bz,
                      GLfloat cx, GLfloat cy, GLfloat cz,
                      GLfloat dx, GLfloat dy, GLfloat dz)
{
    glNormal3f(nx, ny, nz);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(ax, ay, az);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(bx, by, bz);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(cx, cy, cz);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(dx, dy, dz);
}

static void draw_cube(void)
{
    glBegin(GL_QUADS);
    draw_face( 0, 0, 1, -1,-1, 1,  1,-1, 1,  1, 1, 1, -1, 1, 1);
    draw_face( 0, 0,-1,  1,-1,-1, -1,-1,-1, -1, 1,-1,  1, 1,-1);
    draw_face( 1, 0, 0,  1,-1, 1,  1,-1,-1,  1, 1,-1,  1, 1, 1);
    draw_face(-1, 0, 0, -1,-1,-1, -1,-1, 1, -1, 1, 1, -1, 1,-1);
    draw_face( 0, 1, 0, -1, 1, 1,  1, 1, 1,  1, 1,-1, -1, 1,-1);
    draw_face( 0,-1, 0, -1,-1,-1,  1,-1,-1,  1,-1, 1, -1,-1, 1);
    glEnd();
}

static void setup_projection(uint32_t width, uint32_t height)
{
    GLdouble aspect = height ? (GLdouble)width / (GLdouble)height : 1.0;
    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-aspect, aspect, -1.0, 1.0, 1.5, 40.0);
    glMatrixMode(GL_MODELVIEW);
}

static void draw_frame(uint32_t frame, uint32_t width, uint32_t height)
{
    static const GLfloat fog_color[4] = {0.04f, 0.05f, 0.08f, 1.0f};

    glDisable(GL_SCISSOR_TEST);
    glClearColor(fog_color[0], fog_color[1], fog_color[2], fog_color[3]);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_SCISSOR_TEST);
    glScissor(8, 8, (GLsizei)(width > 16U ? width - 16U : width),
                    (GLsizei)(height > 16U ? height - 16U : height));

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 4.0f);
    glFogf(GL_FOG_END, 16.0f);
    glFogfv(GL_FOG_COLOR, fog_color);

    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -7.0f);
    glRotatef((GLfloat)(frame % 360U), 0.35f, 1.0f, 0.15f);
    glColor4f(0.90f, 0.88f, 0.82f, 1.0f);
    draw_cube();

    /* Wireframe/overlay path with polygon offset to avoid z fighting. */
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(1.0f);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glPolygonOffset(-1.0f, -1.0f);
    glColor4f(0.2f, 0.9f, 1.0f, 1.0f);
    draw_cube();
    glDisable(GL_POLYGON_OFFSET_LINE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    /* Normal alpha blending. */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glColor4f(0.9f, 0.25f, 0.2f, 0.35f);
    glBegin(GL_QUADS);
    glVertex3f(-1.6f,-1.2f, 0.5f); glVertex3f( 1.6f,-1.2f, 0.5f);
    glVertex3f( 1.6f, 1.2f, 0.5f); glVertex3f(-1.6f, 1.2f, 0.5f);
    glEnd();

    /* Additive viewport effect. */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.15f, 0.45f, 1.0f, 0.30f);
    glBegin(GL_TRIANGLES);
    glVertex3f( 0.0f, 1.8f, 0.3f);
    glVertex3f(-1.5f,-1.0f, 0.3f);
    glVertex3f( 1.5f,-1.0f, 0.3f);
    glEnd();

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
}

int mesa_gpu_strict_stress(uint32_t frame_count, uint32_t churn_count,
                           mesa_gpu_strict_report_t *report)
{
    bk_mesa_context_t *ctx;
    bk_mesa_gpu_stats_t stats;
    uint32_t width = 320U, height = 240U;
    uint32_t i;

    if (!report) return -1;
    report_clear(report);
    if (!frame_count) frame_count = 10000U;
    if (!churn_count) churn_count = 64U;
    report->frames_requested = frame_count;
    report->churn_requested = churn_count;

    ctx = bk_mesa_create_ex_renderer(width, height, 16U, 0U, 0U,
                                     BK_MESA_RENDERER_GPU_STRICT);
    if (!ctx) return -2;
    if (!bk_mesa_gpu_available(ctx)) {
        report->failure = bk_mesa_last_fallback(ctx);
        bk_mesa_destroy(ctx);
        return -3;
    }

    setup_projection(width, height);
    setup_texture();
    setup_lights();

    for (i = 0U; i < frame_count; ++i) {
        if (i == frame_count / 3U) {
            width = 400U; height = 300U;
            if (!bk_mesa_resize(ctx, width, height)) {
                bk_mesa_destroy(ctx);
                return -6;
            }
            setup_projection(width, height);
        }
        else if (i == (frame_count * 2U) / 3U) {
            width = 320U; height = 240U;
            if (!bk_mesa_resize(ctx, width, height)) {
                bk_mesa_destroy(ctx);
                return -6;
            }
            setup_projection(width, height);
        }

        draw_frame(i, width, height);
        if (bk_mesa_strict_failed(ctx)) {
            report->failure = bk_mesa_last_fallback(ctx);
            bk_mesa_get_gpu_stats(ctx, &report->stats);
            bk_mesa_destroy(ctx);
            return -4;
        }
        if (!bk_mesa_present_gpu(ctx)) {
            report->failure = bk_mesa_last_fallback(ctx);
            bk_mesa_get_gpu_stats(ctx, &report->stats);
            bk_mesa_destroy(ctx);
            return -5;
        }
        report->frames_completed++;
    }

    bk_mesa_get_gpu_stats(ctx, &stats);
    report->stats = stats;
    report->failure = bk_mesa_last_fallback(ctx);
    if (stats.fallbacks || stats.strict_failures || stats.downloads ||
        !stats.draw_calls || !stats.triangles) {
        bk_mesa_destroy(ctx);
        return -7;
    }
    bk_mesa_destroy(ctx);

    for (i = 0U; i < churn_count; ++i) {
        ctx = bk_mesa_create_ex_renderer(64U, 64U, 16U, 0U, 0U,
                                         BK_MESA_RENDERER_GPU_STRICT);
        if (!ctx || !bk_mesa_gpu_available(ctx)) {
            if (ctx) bk_mesa_destroy(ctx);
            return -8;
        }
        setup_projection(64U, 64U);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBegin(GL_TRIANGLES);
        glColor3f(1,0,0); glVertex3f(-0.5f,-0.5f,-3.0f);
        glColor3f(0,1,0); glVertex3f( 0.5f,-0.5f,-3.0f);
        glColor3f(0,0,1); glVertex3f( 0.0f, 0.5f,-3.0f);
        glEnd();
        if (!bk_mesa_present_gpu(ctx) || bk_mesa_strict_failed(ctx)) {
            bk_mesa_destroy(ctx);
            return -8;
        }
        bk_mesa_destroy(ctx);
        report->churn_completed++;
    }
    return 0;
}
