#ifndef BLESKERNOS_DOOM_PORT_H
#define BLESKERNOS_DOOM_PORT_H

#include "../../gui/gui.h"
#include "../../kernel/include/types.h"

void doom_host_attach(gui_window_t *window, uint32_t *framebuffer,
                      uint16_t width, uint16_t height);
void doom_host_detach(void);

#endif
