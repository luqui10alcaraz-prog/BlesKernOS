#include "../kernel/include/api.h"
#include <stdio.h>

/*
 * Lanzador provisional explicito:
 *   /SYSTEM/PROGRAMS/WINE.O /SYSTEM/WIN32/HELLO.EXE
 *
 * El File Browser tambien puede abrir .EXE directamente. Este wrapper queda
 * como punto de entrada visible del futuro subsistema Wine.
 */
void bleskernos_program_main(gui_desktop_t *desktop UNUSED) {
    const char *path = bk_app_launch_argument();

    if (!path || !path[0]) {
        kprintf("[WINE] uso: WINE.O /RUTA/PROGRAMA.EXE\n");
        return;
    }
    kprintf("[WINE] cargando %s\n", path);
    if (!bk_app_pe_execute(path))
        kprintf("[WINE] error: %s\n", bk_app_pe_last_error());
}
