#include <bleskernos_tinygl.h>

/* Minimal SDK example. Blit bk_tinygl_pixels(context) into a GUI surface. */
int tinygl_triangle_render(void)
{
    bk_tinygl_context_t *context = bk_tinygl_create(320, 200);
    if (!context) return -1;

    glViewport(0, 0, 320, 200);
    glClearColor(0.05f, 0.05f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 0.0f, 0.0f); glVertex3f(-0.8f, -0.7f, 0.0f);
    glColor3f(0.0f, 1.0f, 0.0f); glVertex3f( 0.8f, -0.7f, 0.0f);
    glColor3f(0.0f, 0.0f, 1.0f); glVertex3f( 0.0f,  0.8f, 0.0f);
    glEnd();

    if (!bk_tinygl_present(context)) {
        bk_tinygl_destroy(context);
        return -2;
    }

    bk_tinygl_destroy(context);
    return 0;
}
