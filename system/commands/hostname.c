#include "common.h"

/*
 * Stable local node name until SYSTEM.CFG/BlesNet exposes persistent host
 * configuration through the public SDK.
 */
static int run(int argc, char **argv)
{
    if (argc > 1)
        return command_error("hostname",
                             "cambiar el nombre de host aun no esta soportado");

    (void)argv;
    kprintf("BLESKERNOS\n");
    return 0;
}

BK_COMMAND_MAIN(run)
