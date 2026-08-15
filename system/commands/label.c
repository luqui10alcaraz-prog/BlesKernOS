#include "common.h"

static int run(int argc UNUSED, char **argv UNUSED) {
    bk_volume_info_t volume;
    if (!bk_device_volume_info(&volume))
        return command_error("label", "@HD9CD592F");
    kprintf("%s\n", volume.volume_label[0] ? volume.volume_label : "@H8E9220C9");
    return 0;
}

BK_COMMAND_MAIN(run)
