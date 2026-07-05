#include "include/types.h"
#include "include/vga.h"
#include "include/pic.h"

#define VGA_ADDRESS  0x000B8000
#define VGA_WIDTH    80
#define VGA_HEIGHT   25

static volatile uint16_t *vga_buf = (volatile uint16_t *)VGA_ADDRESS;
static int cur_x = 0;
static int cur_y = 0;
static uint8_t cur_color = 0;
static vga_output_char_t output_sink = NULL;
static vga_output_clear_t clear_sink = NULL;
static void *output_context = NULL;

static inline uint8_t make_color(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)((bg << 4) | fg);
}

static inline uint16_t make_entry(char c, uint8_t color) {
    return (uint16_t)((uint16_t)color << 8 | (uint8_t)c);
}

static void update_hw_cursor(void) {
    uint16_t pos = (uint16_t)(cur_y * VGA_WIDTH + cur_x);
    outb(0x3D4, 14);
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
}

static void serial_putchar(char c) {
    static bool initialized = false;
    if (!initialized) {
        outb(0x3F8 + 1, 0x00);
        outb(0x3F8 + 3, 0x80);
        outb(0x3F8 + 0, 0x03);
        outb(0x3F8 + 1, 0x00);
        outb(0x3F8 + 3, 0x03);
        outb(0x3F8 + 2, 0xC7);
        outb(0x3F8 + 4, 0x0B);
        initialized = true;
    }
    while ((inb(0x3F8 + 5) & 0x20) == 0) {}
    outb(0x3F8, (uint8_t)c);
}

static void scroll(void) {
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buf[y * VGA_WIDTH + x] = vga_buf[(y + 1) * VGA_WIDTH + x];
        }
    }
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga_buf[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = make_entry(' ', cur_color);
    }
    cur_y = VGA_HEIGHT - 1;
}

void vga_init(void) {
    cur_color = make_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

void vga_clear(void) {
    if (clear_sink) {
        clear_sink(output_context);
        return;
    }
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buf[i] = make_entry(' ', cur_color);
    }
    cur_x = 0;
    cur_y = 0;
    update_hw_cursor();
}

void vga_set_color(vga_color_t fg, vga_color_t bg) {
    cur_color = make_color(fg, bg);
}

void vga_set_cursor(int x, int y) {
    cur_x = x;
    cur_y = y;
    update_hw_cursor();
}

void vga_get_cursor(int *x, int *y) {
    if (x) *x = cur_x;
    if (y) *y = cur_y;
}

void vga_putchar(char c) {
    if (output_sink) {
        output_sink(c, output_context);
        return;
    }
    serial_putchar(c);
    if (c == '\n') {
        cur_x = 0;
        cur_y++;
    } else if (c == '\r') {
        cur_x = 0;
    } else if (c == '\t') {
        cur_x = (cur_x + 4) & ~3;
    } else if (c == '\b') {
        if (cur_x > 0) {
            cur_x--;
            vga_buf[cur_y * VGA_WIDTH + cur_x] = make_entry(' ', cur_color);
        }
    } else {
        vga_buf[cur_y * VGA_WIDTH + cur_x] = make_entry(c, cur_color);
        cur_x++;
        if (cur_x >= VGA_WIDTH) {
            cur_x = 0;
            cur_y++;
        }
    }

    if (cur_y >= VGA_HEIGHT) {
        scroll();
    }

    update_hw_cursor();
}

void vga_set_output_sink(vga_output_char_t output,
                         vga_output_clear_t clear,
                         void *context) {
    output_sink = output;
    clear_sink = clear;
    output_context = context;
}

void vga_puts(const char *s) {
    while (*s) {
        vga_putchar(*s++);
    }
}

void vga_puthex(uint32_t n) {
    const char *hex = "0123456789ABCDEF";
    vga_puts("0x");
    for (int i = 7; i >= 0; i--) {
        vga_putchar(hex[(n >> (i * 4)) & 0xF]);
    }
}

void vga_putdec(uint32_t n) {
    char buf[11];
    int i = 10;
    buf[10] = '\0';
    if (n == 0) {
        vga_putchar('0');
        return;
    }
    while (n > 0) {
        buf[--i] = '0' + (n % 10);
        n /= 10;
    }
    vga_puts(&buf[i]);
}

void vga_putint(int32_t n) {
    if (n < 0) {
        vga_putchar('-');
        vga_putdec((uint32_t)(-n));
    } else {
        vga_putdec((uint32_t)n);
    }
}

void kprintf(const char *fmt, ...) {
    uint32_t *args = (uint32_t *)(&fmt) + 1;
    int arg_idx = 0;

    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') {
            vga_putchar(fmt[i]);
            continue;
        }
        i++;
        switch (fmt[i]) {
            case 's': {
                const char *s = (const char *)args[arg_idx++];
                vga_puts(s ? s : "(null)");
                break;
            }
            case 'd': {
                int32_t v = (int32_t)args[arg_idx++];
                vga_putint(v);
                break;
            }
            case 'u': {
                uint32_t v = args[arg_idx++];
                vga_putdec(v);
                break;
            }
            case 'x':
            case 'X': {
                uint32_t v = args[arg_idx++];
                vga_puthex(v);
                break;
            }
            case 'c': {
                char c = (char)args[arg_idx++];
                vga_putchar(c);
                break;
            }
            case '%':
                vga_putchar('%');
                break;
            default:
                vga_putchar(fmt[i]);
                break;
        }
    }
}
