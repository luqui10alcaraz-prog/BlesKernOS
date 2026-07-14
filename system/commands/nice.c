#include "common.h"

static int run(int argc UNUSED,char **argv UNUSED){ return command_error("nice","el planificador aun no expone prioridades"); }

BK_COMMAND_MAIN(run)
