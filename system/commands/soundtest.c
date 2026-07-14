#include "common.h"

static int run(int argc UNUSED,char **argv UNUSED){ kprintf("Audio: %s\n",bk_sound_pcm_name());return bk_sound_tone(880,250)?0:1; }

BK_COMMAND_MAIN(run)
