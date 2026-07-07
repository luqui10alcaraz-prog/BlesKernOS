#ifndef VGA_H
#define VGA_H

#include "types.h"

/* Colores VGA */
typedef enum {
    VGA_BLACK         = 0,
    VGA_BLUE          = 1,
    VGA_GREEN         = 2,
    VGA_CYAN          = 3,
    VGA_RED           = 4,
    VGA_MAGENTA       = 5,
    VGA_BROWN         = 6,
    VGA_LIGHT_GREY    = 7,
    VGA_DARK_GREY     = 8,
    VGA_LIGHT_BLUE    = 9,
    VGA_LIGHT_GREEN   = 10,
    VGA_LIGHT_CYAN    = 11,
    VGA_LIGHT_RED     = 12,
    VGA_LIGHT_MAGENTA = 13,
    VGA_YELLOW        = 14,
    VGA_WHITE         = 15,
} vga_color_t;

typedef void (*vga_output_char_t)(char c, void *context);
typedef void (*vga_output_clear_t)(void *context);

void vga_init(void);
void vga_clear(void);
void vga_set_color(vga_color_t fg, vga_color_t bg);
void vga_putchar(char c);
void vga_puts(const char *s);
void vga_puthex(uint32_t n);
void vga_putdec(uint32_t n);
void vga_putint(int32_t n);
void vga_set_cursor(int x, int y);
void vga_get_cursor(int *x, int *y);
void vga_set_output_sink(vga_output_char_t output,
                         vga_output_clear_t clear,
                         void *context);
void kprintf(const char *fmt, ...);

#endif
