#ifndef BLESKERNOS_NETSURF_PLOTTERS_H
#define BLESKERNOS_NETSURF_PLOTTERS_H
#include <bleskernos_api.h>
#include "layout.h"
typedef struct { bk_gui_surface_t *surface; bk_gui_rect_t clip; } nsbk_plotter_t;
void nsbk_plotter_init(nsbk_plotter_t *, bk_gui_surface_t *, bk_gui_rect_t);
void nsbk_plot_fill(nsbk_plotter_t *, bk_gui_rect_t, uint32_t);
void nsbk_plot_rect(nsbk_plotter_t *, bk_gui_rect_t, uint32_t);
void nsbk_plot_line(nsbk_plotter_t *, int, int, int, int, uint32_t);
void nsbk_plot_text(nsbk_plotter_t *, int, int, const char *, uint32_t,
                    uint32_t, uint8_t, bool, bool, bool);
void nsbk_plot_bitmap(nsbk_plotter_t *, bk_gui_rect_t, const bk_gui_image_t *);
#endif
