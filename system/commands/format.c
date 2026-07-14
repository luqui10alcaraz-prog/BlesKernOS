#include "common.h"

static int run(int argc, char **argv) {
    if (argc != 4 || !command_is(argv[3], "/YES")) {
        bk_console_write("Uso: format dispositivo etiqueta /YES\n");
        bk_console_write("ADVERTENCIA: borra todos los datos. No permite formatear el volumen activo.\n");
        return 2;
    }
    kprintf("Formateando %s como FAT, etiqueta %s...\n", argv[1], argv[2]);
    if (!bk_device_format_fat(argv[1], argv[2]))
        return command_error("format", "fallo: disco inexistente, protegido, incompatible o actualmente montado");
    bk_console_write("Formato completado. Use mount para montar el nuevo volumen.\n");
    return 0;
}

BK_COMMAND_MAIN(run)
