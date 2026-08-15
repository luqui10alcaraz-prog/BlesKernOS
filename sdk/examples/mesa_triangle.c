#include <bleskernos_mesa.h>

/* Minimal GPU-aware smoke test.  AUTO uses the active GFX3D driver when the
 * OpenGL state is supported and otherwise keeps Mesa's software fallback. */
int mesa_triangle_render(uint32_t *target, uint32_t width,
                         uint32_t height, uint32_t pitch_bytes)
{
    bk_mesa_context_t *mesa;
    bk_mesa_gpu_stats_t stats;

    mesa = bk_mesa_create_for_buffer_renderer(
        width, height, target, pitch_bytes, 16U, 0U, 0U,
        BK_MESA_RENDERER_AUTO);
    if (!mesa) return 0;

    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.06f, 0.08f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 0.1f, 0.1f); glVertex2f( 0.0f,  0.8f);
    glColor3f(0.1f, 1.0f, 0.1f); glVertex2f(-0.8f, -0.7f);
    glColor3f(0.1f, 0.2f, 1.0f); glVertex2f( 0.8f, -0.7f);
    glEnd();

    /* Resolve to target[] for a normal GUI blit.  A compositor with GFX3D
     * support can instead call bk_mesa_present_gpu() and use the returned
     * surface handle directly, avoiding the download. */
    if (!bk_mesa_present(mesa)) {
        bk_mesa_destroy(mesa);
        return 0;
    }

    bk_mesa_get_gpu_stats(mesa, &stats);
    bk_mesa_destroy(mesa);
    return stats.triangles ? 2 : 1; /* 2 = GPU path, 1 = software fallback. */
}
