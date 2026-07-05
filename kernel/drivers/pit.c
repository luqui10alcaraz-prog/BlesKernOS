#include "../include/types.h"
#include "../include/pit.h"
#include "../include/pic.h"
#include "../include/idt.h"
#include "../include/sound.h"
#include "../include/task.h"

volatile uint32_t kernel_ticks = 0;
static uint32_t pit_frequency_hz = 500;

static void pit_irq_handler(registers_t *regs UNUSED) {
    kernel_ticks++;
    sound_poll();
}

void pit_init(void) {
    uint16_t divisor = 1193182 / pit_frequency_hz;
    if (divisor < 1) divisor = 1;
    if (divisor > 0xFFFF) divisor = 0xFFFF;

    cli();
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    sti();

    irq_install_handler(0, pit_irq_handler);
}

uint32_t pit_get_ticks(void) {
    return kernel_ticks;
}

uint32_t pit_get_uptime_seconds(void) {
    return kernel_ticks / pit_frequency_hz;
}

uint32_t pit_get_frequency_hz(void) {
    return pit_frequency_hz;
}
