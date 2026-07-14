#include "common.h"

static int run(int argc UNUSED,char **argv UNUSED){ return command_error("attrib","el VFS FAT aun no expone atributos editables"); }

BK_COMMAND_MAIN(run)
