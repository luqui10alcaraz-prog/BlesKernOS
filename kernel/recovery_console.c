#include "include/recovery_console.h"
#include "include/types.h"

#define VGA_MISC_WRITE       0x3C2
#define VGA_SEQ_INDEX        0x3C4
#define VGA_SEQ_DATA         0x3C5
#define VGA_CRTC_INDEX       0x3D4
#define VGA_CRTC_DATA        0x3D5
#define VGA_GC_INDEX         0x3CE
#define VGA_GC_DATA          0x3CF
#define VGA_AC_INDEX         0x3C0
#define VGA_INPUT_STATUS_1   0x3DA
#define VGA_TEXT_MEMORY      ((volatile uint16_t *)0xB8000)

static inline void recovery_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t recovery_inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void vga_write_indexed(uint16_t index_port, uint16_t data_port,
                              uint8_t index, uint8_t value) {
    recovery_outb(index_port, index);
    recovery_outb(data_port, value);
}

/* Registros VGA para modo texto color 80x25, fuente 8x16. */
static const uint8_t seq_regs[5] = {
    0x03, 0x00, 0x03, 0x00, 0x02
};

static const uint8_t crtc_regs[25] = {
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
    0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
    0xFF
};

static const uint8_t gc_regs[9] = {
    0x00, 0x00, 0x10, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF
};

static const uint8_t ac_regs[21] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x0C, 0x00, 0x0F, 0x08, 0x00
};

void recovery_console_enter(void) {
    uint32_t i;

    /* Apaga momentaneamente la pantalla durante la reprogramacion. */
    vga_write_indexed(VGA_SEQ_INDEX, VGA_SEQ_DATA, 0x01, 0x20);

    recovery_outb(VGA_MISC_WRITE, 0x67);

    for (i = 0; i < 5; ++i)
        vga_write_indexed(VGA_SEQ_INDEX, VGA_SEQ_DATA, (uint8_t)i, seq_regs[i]);

    /* Desbloquea CRTC 0-7. */
    vga_write_indexed(VGA_CRTC_INDEX, VGA_CRTC_DATA, 0x11,
                      (uint8_t)(recovery_inb(VGA_CRTC_DATA) & 0x7F));

    for (i = 0; i < 25; ++i)
        vga_write_indexed(VGA_CRTC_INDEX, VGA_CRTC_DATA,
                          (uint8_t)i, crtc_regs[i]);

    for (i = 0; i < 9; ++i)
        vga_write_indexed(VGA_GC_INDEX, VGA_GC_DATA, (uint8_t)i, gc_regs[i]);

    for (i = 0; i < 21; ++i) {
        (void)recovery_inb(VGA_INPUT_STATUS_1);
        recovery_outb(VGA_AC_INDEX, (uint8_t)i);
        recovery_outb(VGA_AC_INDEX, ac_regs[i]);
    }

    /* Reactiva video y deshabilita blanking del Attribute Controller. */
    (void)recovery_inb(VGA_INPUT_STATUS_1);
    recovery_outb(VGA_AC_INDEX, 0x20);
    vga_write_indexed(VGA_SEQ_INDEX, VGA_SEQ_DATA, 0x01, seq_regs[1]);

    /* Limpia la consola y deja cursor visible en la esquina superior. */
    for (i = 0; i < 80u * 25u; ++i)
        VGA_TEXT_MEMORY[i] = (uint16_t)0x0720;

    vga_write_indexed(VGA_CRTC_INDEX, VGA_CRTC_DATA, 0x0A, 0x0E);
    vga_write_indexed(VGA_CRTC_INDEX, VGA_CRTC_DATA, 0x0B, 0x0F);
    vga_write_indexed(VGA_CRTC_INDEX, VGA_CRTC_DATA, 0x0E, 0x00);
    vga_write_indexed(VGA_CRTC_INDEX, VGA_CRTC_DATA, 0x0F, 0x00);
}
