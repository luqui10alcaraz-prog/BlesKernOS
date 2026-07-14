#include "../include/mouse.h"
#include "../include/keyboard.h"
#include "../include/pic.h"
#include "../include/idt.h"
#include "../include/vga.h"
#include "../include/gfx.h"
#include "../include/driver.h"

#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_INPUT_FULL  0x02
#define PS2_STATUS_AUX_DATA    0x20

#define PS2_CMD_READ_CONFIG    0x20
#define PS2_CMD_WRITE_CONFIG   0x60
#define PS2_CMD_DISABLE_FIRST  0xAD
#define PS2_CMD_ENABLE_FIRST   0xAE
#define PS2_CMD_DISABLE_AUX    0xA7
#define PS2_CMD_ENABLE_AUX     0xA8
#define PS2_CMD_WRITE_AUX      0xD4

#define MOUSE_ACK              0xFA
#define MOUSE_SET_DEFAULTS     0xF6
#define MOUSE_ENABLE_STREAM    0xF4
#define MOUSE_DISABLE_STREAM   0xF5

#define MOUSE_ERR_NONE         0
#define MOUSE_ERR_TIMEOUT      1
#define MOUSE_ERR_NO_ACK       2
#define MOUSE_ERR_INIT_FAILED  3
#define MOUSE_ERR_ENABLE_AUX   4
#define MOUSE_ERR_READ_CONFIG  5
#define MOUSE_ERR_WRITE_CONFIG 6
#define MOUSE_ERR_SET_DEFAULTS 7
#define MOUSE_ERR_ENABLE_STREAM 8

static volatile mouse_state_t g_mouse;
static volatile uint8_t g_packet[4];
static volatile uint8_t g_packet_index = 0;
static int32_t g_bound_w = 320;
static int32_t g_bound_h = 200;
static uint8_t g_sensitivity = 3;

static int32_t mouse_scale_delta(int32_t value) {
    static const uint8_t scale_percent[5] = {40, 70, 100, 130, 170};
    uint32_t magnitude;
    uint32_t scaled;
    uint8_t percent;

    if (!value) return 0;
    if (g_sensitivity < 1) g_sensitivity = 1;
    if (g_sensitivity > 5) g_sensitivity = 5;
    percent = scale_percent[g_sensitivity - 1];
    if (percent == 100) return value;

    magnitude = (uint32_t)(value < 0 ? -value : value);
    scaled = (magnitude * percent + 50U) / 100U;
    if (scaled == 0U) scaled = 1U;
    return value < 0 ? -(int32_t)scaled : (int32_t)scaled;
}

static bool ps2_wait_input_clear(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if ((inb(KBD_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0) return true;
        __asm__ volatile ("pause");
    }
    return false;
}

static bool ps2_wait_output_full(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if (inb(KBD_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) return true;
        __asm__ volatile ("pause");
    }
    return false;
}

static void ps2_flush(void) {
    for (uint32_t i = 0; i < 256 && (inb(KBD_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL); i++) {
        (void)inb(KBD_DATA_PORT);
    }
}

static bool ps2_write_cmd(uint8_t cmd) {
    if (!ps2_wait_input_clear()) return false;
    outb(KBD_STATUS_PORT, cmd);
    return true;
}

static bool ps2_write_data(uint8_t data) {
    if (!ps2_wait_input_clear()) return false;
    outb(KBD_DATA_PORT, data);
    return true;
}

static bool ps2_read_data(uint8_t *data) {
    if (!data || !ps2_wait_output_full()) return false;
    *data = inb(KBD_DATA_PORT);
    return true;
}

static bool ps2_read_aux_data(uint8_t *data) {
    uint8_t status;

    if (!data) return false;
    for (uint32_t i = 0; i < 100000; i++) {
        status = inb(KBD_STATUS_PORT);
        if (status & PS2_STATUS_OUTPUT_FULL) {
            uint8_t value = inb(KBD_DATA_PORT);
            if (status & PS2_STATUS_AUX_DATA) {
                *data = value;
                return true;
            }
        }
        __asm__ volatile ("pause");
    }
    return false;
}

static bool mouse_write(uint8_t value) {
    uint8_t ack = 0;
    for (uint8_t retry = 0; retry < 3; retry++) {
        if (!ps2_write_cmd(PS2_CMD_WRITE_AUX)) return false;
        if (!ps2_write_data(value)) return false;
        if (!ps2_read_aux_data(&ack)) {
            g_mouse.last_error = MOUSE_ERR_TIMEOUT;
            return false;
        }
        if (ack == MOUSE_ACK) return true;
    }
    g_mouse.last_error = MOUSE_ERR_NO_ACK;
    return false;
}

static void mouse_apply_bounds(void) {
    if (g_mouse.x < 0) g_mouse.x = 0;
    if (g_mouse.y < 0) g_mouse.y = 0;
    if (g_mouse.x >= g_bound_w) g_mouse.x = g_bound_w - 1;
    if (g_mouse.y >= g_bound_h) g_mouse.y = g_bound_h - 1;
}

static void mouse_handle_packet(void) {
    uint8_t b0 = g_packet[0];
    int32_t dx;
    int32_t dy;

    if ((b0 & 0x08) == 0) return;
    if (b0 & 0xC0) return;

    dx = (int32_t)g_packet[1];
    dy = (int32_t)g_packet[2];
    if (b0 & 0x10) dx |= 0xFFFFFF00;
    if (b0 & 0x20) dy |= 0xFFFFFF00;
    dx = mouse_scale_delta(dx);
    dy = mouse_scale_delta(dy);

    g_mouse.dx = dx;
    g_mouse.dy = dy;
    g_mouse.x += dx;
    g_mouse.y -= dy;
    g_mouse.buttons = b0 & 0x07;

    if (g_mouse.packet_size == 4) {
        int8_t wheel = (int8_t)(g_packet[3] & 0x0F);
        if (wheel & 0x08) wheel |= 0xF0;
        g_mouse.wheel += wheel;
        if (g_mouse.device_id == 4) {
            g_mouse.buttons |= (g_packet[3] & 0x30);
        }
    }

    mouse_apply_bounds();
    g_mouse.packets++;
}

static void mouse_irq_handler(registers_t *regs UNUSED) {
    uint8_t status = inb(KBD_STATUS_PORT);
    uint8_t data;

    if ((status & PS2_STATUS_OUTPUT_FULL) == 0) return;
    if ((status & PS2_STATUS_AUX_DATA) == 0) return;
    data = inb(KBD_DATA_PORT);
    g_mouse.irq_count++;
    g_mouse.byte_count++;

    if (g_packet_index == 0 && (data & 0x08) == 0) return;
    g_packet[g_packet_index++] = data;
    if (g_packet_index >= g_mouse.packet_size) {
        g_packet_index = 0;
        mouse_handle_packet();
    }
}

void mouse_set_bounds(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) return;
    g_bound_w = width;
    g_bound_h = height;
    mouse_apply_bounds();
}

void mouse_set_position(int32_t x, int32_t y) {
    g_mouse.x = x;
    g_mouse.y = y;
    mouse_apply_bounds();
}

void mouse_set_sensitivity(uint8_t sensitivity) {
    if (sensitivity < 1) sensitivity = 1;
    if (sensitivity > 5) sensitivity = 5;
    g_sensitivity = sensitivity;
}

uint8_t mouse_get_sensitivity(void) {
    return g_sensitivity;
}

bool mouse_is_present(void) {
    return g_mouse.present;
}

void mouse_get_state(mouse_state_t *state) {
    if (!state) return;
    cli();
    *state = g_mouse;
    sti();
}

void mouse_init(void) {
    uint8_t config = 0;
    const gfx_info_t *gfx = gfx_get_info();

    if (gfx && gfx->width && gfx->height) {
        g_bound_w = gfx->width;
        g_bound_h = gfx->height;
    }

    g_mouse.present = false;
    g_mouse.device_id = 0;
    g_mouse.packet_size = 3;
    g_mouse.x = g_bound_w / 2;
    g_mouse.y = g_bound_h / 2;
    g_mouse.dx = 0;
    g_mouse.dy = 0;
    g_mouse.wheel = 0;
    g_mouse.buttons = 0;
    g_mouse.packets = 0;
    g_mouse.irq_count = 0;
    g_mouse.byte_count = 0;
    g_mouse.init_step = 0;
    g_mouse.last_error = MOUSE_ERR_NONE;
    g_packet_index = 0;
    g_sensitivity = 3;

    ps2_flush();
    g_mouse.init_step = 1;
    (void)ps2_write_cmd(PS2_CMD_DISABLE_FIRST);
    (void)ps2_write_cmd(PS2_CMD_DISABLE_AUX);
    ps2_flush();

    g_mouse.init_step = 2;
    if (!ps2_write_cmd(PS2_CMD_READ_CONFIG)) {
        g_mouse.last_error = MOUSE_ERR_READ_CONFIG;
        return;
    }
    if (!ps2_read_data(&config)) {
        g_mouse.last_error = MOUSE_ERR_READ_CONFIG;
        return;
    }
    config |= 0x02;
    config &= (uint8_t)~0x20;
    g_mouse.init_step = 3;
    if (!ps2_write_cmd(PS2_CMD_WRITE_CONFIG)) {
        g_mouse.last_error = MOUSE_ERR_WRITE_CONFIG;
        return;
    }
    if (!ps2_write_data(config)) {
        g_mouse.last_error = MOUSE_ERR_WRITE_CONFIG;
        return;
    }
    (void)ps2_write_cmd(PS2_CMD_ENABLE_FIRST);
    if (!ps2_write_cmd(PS2_CMD_ENABLE_AUX)) {
        g_mouse.last_error = MOUSE_ERR_ENABLE_AUX;
        return;
    }

    g_mouse.init_step = 4;
    (void)mouse_write(MOUSE_DISABLE_STREAM);
    if (!mouse_write(MOUSE_SET_DEFAULTS)) {
        if (g_mouse.last_error == MOUSE_ERR_NONE) {
            g_mouse.last_error = MOUSE_ERR_SET_DEFAULTS;
        }
        return;
    }

    g_mouse.init_step = 5;
    g_mouse.device_id = 0;
    g_mouse.packet_size = 3;

    g_mouse.init_step = 6;
    irq_install_handler(12, mouse_irq_handler);
    ps2_flush();
    if (!mouse_write(MOUSE_ENABLE_STREAM)) {
        if (g_mouse.last_error == MOUSE_ERR_NONE) {
            g_mouse.last_error = MOUSE_ERR_ENABLE_STREAM;
        }
        return;
    }
    g_mouse.present = true;
    g_mouse.last_error = MOUSE_ERR_NONE;
    g_mouse.init_step = 7;
    kprintf("  [MOUSE] PS/2 id=%u packet=%u\n", g_mouse.device_id, g_mouse.packet_size);
}

static bool ps2_mouse_driver_init(void) {
    static const mouse_driver_ops_t ops = {
        mouse_is_present,
        mouse_get_state,
        mouse_set_bounds,
        mouse_set_position,
        mouse_set_sensitivity,
        mouse_get_sensitivity
    };

    /* El controlador 8042 es compartido con teclado. Evita que IRQ1/IRQ12
       consuman bytes de respuesta mientras se negocia el dispositivo. */
    cli();
    mouse_init();
    sti();
    return mouse_register_driver(&ops);
}

const bk_driver_module_t *bleskernos_driver_query(void) {
    static const bk_driver_module_t module = {
        BK_DRIVER_ABI_VERSION,
        sizeof(bk_driver_module_t),
        "ps2-mouse",
        "Mouse PS/2 mediante controlador 8042",
        ps2_mouse_driver_init,
        NULL
    };
    return &module;
}
