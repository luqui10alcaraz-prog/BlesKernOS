#ifndef BEX_LOADER_H
#define BEX_LOADER_H

#include "types.h"

struct gui_desktop;

typedef enum {
    BEX_FORMAT_UNKNOWN = 0,
    BEX_FORMAT_BOS1,
    BEX_FORMAT_BOS2,
    BEX_FORMAT_BOS3,
    BEX_FORMAT_BOS4,
    BEX_FORMAT_BLES32
} bex_format_t;

bex_format_t bex_identify(const void *image, uint32_t size);
const char *bex_format_name(bex_format_t format);
bool bex_execute_program(const char *path, struct gui_desktop *desktop,
                         const char *launch_arg);
const char *bex_last_error(void);

#endif
