#include "include/types.h"
#include "include/vga.h"
#include "include/pic.h"
#include "include/task.h"
#include "include/language.h"
#include "include/vfs.h"

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
static uint32_t output_route = 0;

/*
 * Espejo en RAM de todo byte que el kernel intenta enviar por COM1.
 * Se llena incluso cuando el UART no existe o esta deshabilitado en BIOS,
 * permitiendo recuperar el diagnostico desde la terminal grafica.
 *
 * Es lineal, no circular: se conserva siempre el comienzo del arranque, que
 * contiene enumeracion PCI, carga ELF de .DVR y seleccion del backend grafico.
 */
#define VGA_COM1_LOG_CAPACITY (32U * 1024U)
static char com1_log[VGA_COM1_LOG_CAPACITY];
static volatile uint32_t com1_log_length;
static volatile uint32_t com1_log_dropped;

static void com1_log_capture(char c) {
    uint32_t position = com1_log_length;
    if (position < VGA_COM1_LOG_CAPACITY) {
        com1_log[position] = c;
        com1_log_length = position + 1U;
    } else {
        com1_log_dropped++;
    }
}

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
    static bool unavailable = false;
    uint32_t spin;

    /* Capturar antes de tocar el UART: el log funciona aun sin puerto serie. */
    com1_log_capture(c);
    if (unavailable) return;
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

    /*
     * COM1 es solamente un canal de diagnostico. Nunca debe poder detener el
     * kernel si el UART no existe, queda deshabilitado por BIOS o deja de
     * responder. En hardware real la espera anterior era infinita.
     */
    for (spin = 0; spin < 10000U; spin++) {
        if ((inb(0x3F8 + 5) & 0x20) != 0) break;
    }
    if (spin == 10000U) {
        unavailable = true;
        return;
    }
    outb(0x3F8, (uint8_t)c);
}

uint32_t vga_com1_log_size(void) {
    return com1_log_length;
}

uint32_t vga_com1_log_dropped(void) {
    return com1_log_dropped;
}

bool vga_com1_log_save(const char *path) {
    uint32_t snapshot_size;
    if (!path || !path[0]) return false;
    /* El buffer nunca se reubica. Tomar el largo una sola vez produce una
       instantanea coherente aunque aparezca otro mensaje durante la escritura. */
    snapshot_size = com1_log_length;
    return vfs_write_all(path, com1_log, snapshot_size);
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
    if (clear_sink && output_route != 0U &&
        task_current_console_route() == output_route) {
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
    if (output_sink && output_route != 0U &&
        task_current_console_route() == output_route) {
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
    if (output) {
        output_route = task_current_process_id();
        task_set_current_console_route(output_route);
    } else {
        output_route = 0;
        task_set_current_console_route(0);
    }
}

void vga_puts(const char *s) {
    s = language_translate(s);
    while (s && *s) {
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

static void vga_repeat(char character, uint32_t count) {
    while (count--) vga_putchar(character);
}

static uint32_t vga_format_u32(uint32_t value, uint32_t base,
                               bool uppercase, char *buffer) {
    const char *digits = uppercase ? "0123456789ABCDEF"
                                   : "0123456789abcdef";
    char reverse[32];
    uint32_t length = 0U;

    if (base < 2U || base > 16U || !buffer) return 0U;
    do {
        reverse[length++] = digits[value % base];
        value /= base;
    } while (value && length < sizeof(reverse));

    for (uint32_t i = 0U; i < length; i++)
        buffer[i] = reverse[length - i - 1U];
    return length;
}

static void vga_put_number(uint32_t value, bool negative, uint32_t base,
                           bool uppercase, uint32_t width, char padding,
                           bool left_align, bool prefix) {
    char digits[32];
    uint32_t length = vga_format_u32(value, base, uppercase, digits);
    uint32_t decoration = (negative ? 1U : 0U) + (prefix ? 2U : 0U);
    uint32_t spaces = width > length + decoration
        ? width - length - decoration : 0U;

    if (!left_align && padding != '0') vga_repeat(' ', spaces);
    if (negative) vga_putchar('-');
    if (prefix) {
        vga_putchar('0');
        vga_putchar(uppercase ? 'X' : 'x');
    }
    if (!left_align && padding == '0') vga_repeat('0', spaces);
    for (uint32_t i = 0U; i < length; i++) vga_putchar(digits[i]);
    if (left_align) vga_repeat(' ', spaces);
}

void kprintf(const char *fmt, ...) {
    uint32_t *args;
    uint32_t arg_idx = 0U;

    fmt = language_translate(fmt);
    if (!fmt) return;
    args = (uint32_t *)(&fmt) + 1;

    for (uint32_t i = 0U; fmt[i]; i++) {
        uint32_t spec_start;
        uint32_t cursor;
        uint32_t width = 0U;
        bool has_width = false;
        bool zero_pad = false;
        bool left_align = false;
        bool alternate = false;
        char spec;

        if (fmt[i] != '%') {
            vga_putchar(fmt[i]);
            continue;
        }

        spec_start = i;
        cursor = i + 1U;
        if (fmt[cursor] == '%') {
            vga_putchar('%');
            i = cursor;
            continue;
        }

        for (;;) {
            if (fmt[cursor] == '0') zero_pad = true;
            else if (fmt[cursor] == '-') left_align = true;
            else if (fmt[cursor] == '#') alternate = true;
            else break;
            cursor++;
        }
        while (fmt[cursor] >= '0' && fmt[cursor] <= '9') {
            has_width = true;
            if (width < 100000U)
                width = width * 10U + (uint32_t)(fmt[cursor] - '0');
            cursor++;
        }
        /* BlesKernOS remains 32-bit. Accept common length modifiers so they
         * cannot desynchronise the variadic argument list, but consume one
         * 32-bit slot just like the historical formatter. */
        while (fmt[cursor] == 'l' || fmt[cursor] == 'h' ||
               fmt[cursor] == 'z') cursor++;

        spec = fmt[cursor];
        if (!spec) {
            vga_putchar('%');
            break;
        }
        i = cursor;

        switch (spec) {
            case 's': {
                const char *text = (const char *)(uintptr_t)args[arg_idx++];
                uint32_t length = 0U;
                if (!text) text = "(null)";
                while (text[length]) length++;
                if (!left_align && width > length)
                    vga_repeat(' ', width - length);
                while (*text) vga_putchar(*text++);
                if (left_align && width > length)
                    vga_repeat(' ', width - length);
                break;
            }
            case 'd':
            case 'i': {
                int32_t signed_value = (int32_t)args[arg_idx++];
                bool negative = signed_value < 0;
                uint32_t magnitude = negative
                    ? 0U - (uint32_t)signed_value
                    : (uint32_t)signed_value;
                vga_put_number(magnitude, negative, 10U, false, width,
                               zero_pad ? '0' : ' ', left_align, false);
                break;
            }
            case 'u':
                vga_put_number(args[arg_idx++], false, 10U, false, width,
                               zero_pad ? '0' : ' ', left_align, false);
                break;
            case 'x':
            case 'X': {
                bool uppercase = spec == 'X';
                /* Preserve the old kernel convention where bare %x includes
                 * 0x. Explicit-width forms follow normal printf behaviour,
                 * which makes strings such as 0x%04X produce 0x0378. */
                bool prefix = alternate || !has_width;
                vga_put_number(args[arg_idx++], false, 16U, uppercase, width,
                               zero_pad ? '0' : ' ', left_align, prefix);
                break;
            }
            case 'p':
                if (!has_width) width = 10U;
                vga_put_number(args[arg_idx++], false, 16U, false, width,
                               zero_pad ? '0' : ' ', left_align, true);
                break;
            case 'c': {
                char character = (char)args[arg_idx++];
                if (!left_align && width > 1U) vga_repeat(' ', width - 1U);
                vga_putchar(character);
                if (left_align && width > 1U) vga_repeat(' ', width - 1U);
                break;
            }
            default:
                /* Keep malformed/unsupported conversions visible. Most
                 * importantly, never reinterpret an earlier integer as the
                 * pointer of a following %s, which caused the 0x378 #PF. */
                for (uint32_t j = spec_start; j <= cursor; j++)
                    vga_putchar(fmt[j]);
                break;
        }
    }
}
