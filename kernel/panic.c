#include "include/panic.h"
#include "include/gfx.h"
#include "include/pic.h"
#include "include/sound.h"

static void panic_hex(char *out, uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 8; i++)
        out[2 + i] = digits[(value >> ((7 - i) * 4)) & 0x0F];
    out[10] = '\0';
}

static void panic_dec(char *out, uint32_t value) {
    char temp[11];
    int pos = 10;
    temp[pos] = '\0';
    if (!value) temp[--pos] = '0';
    while (value) {
        temp[--pos] = (char)('0' + value % 10);
        value /= 10;
    }
    int target = 0;
    while (temp[pos]) out[target++] = temp[pos++];
    out[target] = '\0';
}

static void panic_beep_three_times(void) {
    for (int beep = 0; beep < 3; beep++) {
        sound_play(880);
        for (uint32_t i = 0; i < 300000; i++) io_wait();
        sound_stop();
        for (uint32_t i = 0; i < 180000; i++) io_wait();
    }
}

static void panic_bomb(int cx, int cy) {
    const int radius = 80;
    const uint32_t white = 0x00FFFFFF;
    for (int y = -radius; y <= radius; y++)
        for (int x = -radius; x <= radius; x++)
            if (x * x + y * y <= radius * radius)
                gfx_putpixel_rgb(cx + x, cy + y, white);

    gfx_fill_rect_rgb(cx + 42, cy - 75, 32, 26, white);
    gfx_fill_rect_rgb(cx + 62, cy - 92, 12, 22, white);
    gfx_fill_rect_rgb(cx + 69, cy - 105, 26, 8, white);
    gfx_fill_rect_rgb(cx + 88, cy - 117, 11, 18, white);

    gfx_fill_rect_rgb(cx + 99, cy - 128, 24, 6, white);
    gfx_fill_rect_rgb(cx + 108, cy - 137, 6, 24, white);
    gfx_fill_rect_rgb(cx + 101, cy - 131, 20, 20, white);
}

void panic_show(const char *message, uint32_t interrupt,
                uint32_t error_code, uint32_t address) {
    const gfx_info_t *info;
    char number[16];
    cli();

    info = gfx_get_info();
    if (!info || (info->mode != GFX_MODE_VESA_LFB &&
                  !gfx_set_mode13h())) {
        for (;;) __asm__ volatile ("hlt");
    }
    info = gfx_get_info();
    gfx_clear_rgb(0x00A90018);

    int width = info->width;
    int height = info->height;
    int left = width / 10;
    int top = height / 10;
    int bomb_x = width / 4 - 8;
    int bomb_y = height / 2;
    int text_x = width / 2 + 5;

    gfx_fill_rect_rgb(0, 0, width, 54, 0x006E000F);
    gfx_fill_rect_rgb(0, 54, width, 2, 0x00FFFFFF);
    gfx_draw_string(left, 18, "BLESKERNOS 1.0  /  KERNEL BK1",
                    15, 0, false);
    gfx_draw_string(left, top + 18,
                    "EL SISTEMA SE DETUVO PARA EVITAR DANOS.",
                    15, 0, false);
    gfx_draw_string(left, top + 38,
                    "SE REGISTRO UN FALLO IRRECUPERABLE DEL KERNEL.",
                    15, 0, false);

    panic_bomb(bomb_x, bomb_y);

    gfx_fill_rect_rgb(text_x - 16, bomb_y - 96,
                      width - text_x - left + 16, 168, 0x00850012);
    gfx_draw_string(text_x, bomb_y - 78, "DIAGNOSTICO", 15, 0, false);
    gfx_draw_string(text_x, bomb_y - 56,
                    message ? message : "KERNEL PANIC", 15, 0, false);

    gfx_draw_string(text_x, bomb_y - 20, "INTERRUPCION:", 15, 0, false);
    panic_dec(number, interrupt);
    gfx_draw_string(text_x + 112, bomb_y - 20, number, 15, 0, false);

    gfx_draw_string(text_x, bomb_y, "ERROR CODE:", 15, 0, false);
    panic_hex(number, error_code);
    gfx_draw_string(text_x + 112, bomb_y, number, 15, 0, false);

    gfx_draw_string(text_x, bomb_y + 20, "DIRECCION:", 15, 0, false);
    panic_hex(number, address);
    gfx_draw_string(text_x + 112, bomb_y + 20, number, 15, 0, false);
    gfx_draw_string(text_x, bomb_y + 46, "ESTADO: CPU DETENIDO",
                    15, 0, false);

    gfx_fill_rect_rgb(0, height - 92, width, 92, 0x006E000F);
    gfx_fill_rect_rgb(0, height - 92, width, 2, 0x00FFFFFF);
    gfx_draw_string(left, height - 68,
                    "ANOTE EL DIAGNOSTICO Y REINICIE EL EQUIPO.",
                    15, 0, false);
    gfx_draw_string(left, height - 46,
                    "SI EL PROBLEMA CONTINUA, REVISE HARDWARE Y DISCO.",
                    15, 0, false);

    panic_beep_three_times();
    for (;;) __asm__ volatile ("hlt");
}
