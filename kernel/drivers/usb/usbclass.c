#include "../../include/driver.h"
#include "../../include/usb_core.h"
#include "../../include/keyboard.h"
#include "../../include/mouse.h"
#include "../../include/lpt.h"
#include "../../include/task.h"
#include "../../include/pit.h"
#include "../../include/memory.h"
#include "../../include/vga.h"

#define USBCLASS_MAX_HID       16U
#define USBCLASS_MAX_HUB        8U
#define USBCLASS_MAX_PRINTER    8U
#define HID_MAX_REPORT_DESC   512U
#define HID_MAX_REPORT          64U
#define HID_MAX_FIELDS         192U
#define HID_MAX_LOCAL_USAGES    32U

#define USB_REQ_GET_DESCRIPTOR  0x06U
#define USB_REQ_CLEAR_FEATURE   0x01U
#define USB_REQ_SET_FEATURE     0x03U

#define USB_RT_IN_STD_INTERFACE   0x81U
#define USB_RT_OUT_CLASS_INTERFACE 0x21U
#define USB_RT_IN_CLASS_INTERFACE  0xA1U
#define USB_RT_IN_CLASS_DEVICE     0xA0U
#define USB_RT_OUT_CLASS_OTHER     0x23U
#define USB_RT_IN_CLASS_OTHER      0xA3U

#define HID_DESC_HID       0x21U
#define HID_DESC_REPORT    0x22U
#define HID_REQ_SET_IDLE   0x0AU
#define HID_REQ_SET_PROTOCOL 0x0BU

#define HUB_DESC_TYPE      0x29U
#define HUB_PORT_ENABLE       1U
#define HUB_PORT_RESET        4U
#define HUB_PORT_POWER        8U
#define HUB_C_PORT_CONNECTION 16U
#define HUB_C_PORT_ENABLE     17U
#define HUB_C_PORT_SUSPEND    18U
#define HUB_C_PORT_OVERCURRENT 19U
#define HUB_C_PORT_RESET      20U
#define HUB_STATUS_CONNECTION (1U << 0)
#define HUB_STATUS_ENABLE     (1U << 1)
#define HUB_STATUS_OVERCURRENT (1U << 3)
#define HUB_STATUS_RESET      (1U << 4)
#define HUB_STATUS_POWER      (1U << 8)
#define HUB_STATUS_LOW_SPEED  (1U << 9)

#define PRINTER_REQ_GET_DEVICE_ID 0U
#define PRINTER_REQ_GET_PORT_STATUS 1U
#define PRINTER_REQ_SOFT_RESET 2U

#define HID_USAGE_PAGE_GENERIC_DESKTOP 0x01U
#define HID_USAGE_PAGE_KEYBOARD        0x07U
#define HID_USAGE_PAGE_BUTTON          0x09U
#define HID_USAGE_PAGE_CONSUMER        0x0CU
#define HID_USAGE_X                    0x30U
#define HID_USAGE_Y                    0x31U
#define HID_USAGE_WHEEL                0x38U
#define HID_USAGE_POINTER              0x01U
#define HID_USAGE_MOUSE                0x02U
#define HID_USAGE_KEYBOARD             0x06U

typedef enum {
    HID_MODE_BOOT_KEYBOARD = 1,
    HID_MODE_BOOT_MOUSE,
    HID_MODE_REPORT
} hid_mode_t;

typedef struct {
    uint8_t report_id;
    uint16_t bit_offset;
    uint8_t bit_size;
    uint16_t usage_page;
    uint16_t usage;
    uint16_t usage_min;
    uint16_t usage_max;
    uint16_t application_page;
    uint16_t application_usage;
    int32_t logical_min;
    int32_t logical_max;
    int32_t last_value;
    bool array;
    bool relative;
    bool seen;
} hid_field_t;

typedef struct {
    bool active;
    usb_device_t *device;
    usb_interface_t *interface;
    usb_endpoint_t *input;
    hid_mode_t mode;
    uint16_t report_length;
    uint16_t report_descriptor_length;
    uint16_t field_count;
    hid_field_t fields[HID_MAX_FIELDS];
    bool report_ids;
    bool keyboard_capable;
    bool pointer_capable;
    bool keys[256];
    uint8_t mouse_buttons;
    uint32_t failures;
} hid_context_t;

typedef struct {
    bool active;
    usb_device_t *device;
    usb_interface_t *interface;
    usb_endpoint_t *status_endpoint;
    uint8_t ports;
    uint16_t characteristics;
    uint16_t power_good_ms;
    uint32_t last_full_poll;
    uint32_t failures;
} hub_context_t;

typedef struct {
    bool active;
    usb_device_t *device;
    usb_interface_t *interface;
    usb_endpoint_t *bulk_out;
    usb_endpoint_t *bulk_in;
    uint8_t protocol;
    uint8_t status;
    char device_id[192];
} printer_context_t;

typedef struct {
    uint16_t usage_page;
    int32_t logical_min;
    int32_t logical_max;
    uint32_t report_size;
    uint32_t report_count;
    uint8_t report_id;
} hid_global_state_t;

typedef struct {
    uint16_t usages[HID_MAX_LOCAL_USAGES];
    uint8_t usage_count;
    uint16_t usage_min;
    uint16_t usage_max;
    bool has_range;
} hid_local_state_t;

typedef struct {
    uint16_t usage_page;
    uint16_t usage;
} hid_application_state_t;

static hid_context_t g_hid[USBCLASS_MAX_HID];
static hub_context_t g_hub[USBCLASS_MAX_HUB];
static printer_context_t g_printer[USBCLASS_MAX_PRINTER];
static bool g_running;

static uint16_t read16le(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_unsigned(const uint8_t *data, uint8_t size) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < size; i++) value |= (uint32_t)data[i] << (i * 8U);
    return value;
}

static int32_t read_signed(const uint8_t *data, uint8_t size) {
    uint32_t value = read_unsigned(data, size);
    if (size && size < 4U && (value & (1U << (size * 8U - 1U))))
        value |= ~((1U << (size * 8U)) - 1U);
    return (int32_t)value;
}

static void sleep_ms(uint32_t milliseconds) {
    uint32_t hz = pit_get_frequency_hz();
    uint32_t ticks;
    if (!hz) hz = 100U;
    ticks = (uint32_t)(((uint64_t)milliseconds * hz + 999U) / 1000U);
    if (!ticks) ticks = 1U;
    task_sleep(ticks);
}

static uint32_t elapsed_ms(uint32_t since) {
    uint32_t hz = pit_get_frequency_hz();
    if (!hz) hz = 100U;
    return (uint32_t)(((uint64_t)(pit_get_ticks() - since) * 1000U) / hz);
}

static int32_t sign_extend_bits(uint32_t value, uint8_t bits) {
    if (!bits || bits >= 32U) return (int32_t)value;
    if (value & (1U << (bits - 1U))) value |= ~((1U << bits) - 1U);
    return (int32_t)value;
}

static uint32_t report_bits(const uint8_t *report, uint32_t length,
                            uint16_t bit_offset, uint8_t bit_size) {
    uint32_t value = 0;
    if (!report || !bit_size || bit_size > 32U ||
        (uint32_t)bit_offset + bit_size > length * 8U) return 0;
    for (uint8_t bit = 0; bit < bit_size; bit++) {
        uint32_t position = (uint32_t)bit_offset + bit;
        if (report[position >> 3] & (1U << (position & 7U)))
            value |= 1U << bit;
    }
    return value;
}

static void hid_local_reset(hid_local_state_t *local) {
    if (local) kmemset(local, 0, sizeof(*local));
}

static uint16_t hid_local_usage(const hid_local_state_t *local,
                                uint32_t index) {
    if (!local) return 0;
    if (index < local->usage_count) return local->usages[index];
    if (local->has_range && local->usage_min + index <= local->usage_max)
        return (uint16_t)(local->usage_min + index);
    if (local->usage_count) return local->usages[local->usage_count - 1U];
    return 0;
}

static bool hid_parse_report_descriptor(hid_context_t *context,
                                        const uint8_t *descriptor,
                                        uint16_t length) {
    hid_global_state_t global;
    hid_global_state_t stack[4];
    uint8_t stack_depth = 0;
    hid_local_state_t local;
    hid_application_state_t application;
    hid_application_state_t application_stack[8];
    uint8_t application_depth = 0;
    uint16_t report_offsets[256];
    uint32_t position = 0;
    kmemset(&global, 0, sizeof(global));
    kmemset(&local, 0, sizeof(local));
    kmemset(&application, 0, sizeof(application));
    kmemset(application_stack, 0, sizeof(application_stack));
    kmemset(report_offsets, 0, sizeof(report_offsets));
    global.report_count = 1U;

    while (position < length) {
        uint8_t prefix = descriptor[position++];
        uint8_t size;
        uint8_t type;
        uint8_t tag;
        uint32_t value;
        int32_t signed_value;
        if (prefix == 0xFEU) {
            if (position + 2U > length) return false;
            size = descriptor[position++];
            position++; /* long item tag */
            if (position + size > length) return false;
            position += size;
            continue;
        }
        size = prefix & 3U;
        if (size == 3U) size = 4U;
        type = (prefix >> 2) & 3U;
        tag = (prefix >> 4) & 0x0FU;
        if (position + size > length) return false;
        value = read_unsigned(descriptor + position, size);
        signed_value = read_signed(descriptor + position, size);
        position += size;

        if (type == 1U) { /* Global items. */
            switch (tag) {
                case 0x0U: global.usage_page = (uint16_t)value; break;
                case 0x1U: global.logical_min = signed_value; break;
                case 0x2U: global.logical_max = signed_value; break;
                case 0x7U: global.report_size = value; break;
                case 0x8U:
                    global.report_id = (uint8_t)value;
                    if (global.report_id) context->report_ids = true;
                    break;
                case 0x9U: global.report_count = value; break;
                case 0xAU:
                    if (stack_depth < 4U) stack[stack_depth++] = global;
                    break;
                case 0xBU:
                    if (stack_depth) global = stack[--stack_depth];
                    break;
                default: break;
            }
        } else if (type == 2U) { /* Local items. */
            if (tag == 0x0U && local.usage_count < HID_MAX_LOCAL_USAGES)
                local.usages[local.usage_count++] = (uint16_t)value;
            else if (tag == 0x1U) {
                local.usage_min = (uint16_t)value;
                local.has_range = true;
            } else if (tag == 0x2U) {
                local.usage_max = (uint16_t)value;
                local.has_range = true;
            }
        } else if (type == 0U) { /* Main items. */
            if (tag == 0x8U) { /* Input */
                bool constant = (value & 1U) != 0U;
                bool variable = (value & 2U) != 0U;
                bool relative = (value & 4U) != 0U;
                uint32_t count = global.report_count ? global.report_count : 1U;
                uint32_t width = global.report_size;
                uint32_t offset = report_offsets[global.report_id];
                uint32_t prefix_bits = global.report_id ? 8U : 0U;
                uint32_t packet_bits = (uint32_t)context->report_length * 8U;
                uint32_t available_bits;
                if (!width || prefix_bits >= packet_bits ||
                    offset > packet_bits - prefix_bits) return false;
                available_bits = packet_bits - prefix_bits - offset;
                if (count > HID_MAX_REPORT * 8U || width > available_bits ||
                    count > available_bits / width) return false;
                if (!constant && width <= 32U &&
                    count > HID_MAX_FIELDS - context->field_count)
                    return false;
                for (uint32_t item = 0; item < count; item++) {
                    uint16_t item_offset = report_offsets[global.report_id];
                    if (!constant && width && width <= 32U &&
                        context->field_count < HID_MAX_FIELDS) {
                        hid_field_t *field = &context->fields[context->field_count++];
                        kmemset(field, 0, sizeof(*field));
                        field->report_id = global.report_id;
                        field->bit_offset = (uint16_t)(item_offset +
                            (global.report_id ? 8U : 0U));
                        field->bit_size = (uint8_t)width;
                        field->usage_page = global.usage_page;
                        field->usage = variable ? hid_local_usage(&local, item) : 0U;
                        field->usage_min = local.usage_min;
                        field->usage_max = local.usage_max;
                        field->application_page = application.usage_page;
                        field->application_usage = application.usage;
                        field->logical_min = global.logical_min;
                        field->logical_max = global.logical_max;
                        field->array = !variable;
                        field->relative = relative;
                    }
                    report_offsets[global.report_id] =
                        (uint16_t)(item_offset + width);
                }
            } else if (tag == 0xAU) { /* Collection */
                uint8_t collection_type = (uint8_t)value;
                if (application_depth < 8U)
                    application_stack[application_depth++] = application;
                if (collection_type == 1U) { /* Application */
                    application.usage_page = global.usage_page;
                    application.usage = hid_local_usage(&local, 0U);
                }
            } else if (tag == 0xCU) { /* End Collection */
                if (application_depth)
                    application = application_stack[--application_depth];
            }
            hid_local_reset(&local);
        }
    }
    for (uint32_t i = 0; i < context->field_count; i++) {
        const hid_field_t *field = &context->fields[i];
        if (field->usage_page == HID_USAGE_PAGE_KEYBOARD)
            context->keyboard_capable = true;
        if (field->application_page == HID_USAGE_PAGE_GENERIC_DESKTOP &&
            (field->application_usage == HID_USAGE_MOUSE ||
             field->application_usage == HID_USAGE_POINTER) &&
            (field->usage_page == HID_USAGE_PAGE_BUTTON ||
             (field->usage_page == HID_USAGE_PAGE_GENERIC_DESKTOP &&
              (field->usage == HID_USAGE_X || field->usage == HID_USAGE_Y ||
               field->usage == HID_USAGE_WHEEL))))
            context->pointer_capable = true;
    }
    return context->field_count != 0U;
}

static uint16_t hid_report_descriptor_length(const usb_interface_t *interface) {
    uint32_t position = 0;
    if (!interface) return 0;
    while (position + 2U <= interface->extra_length) {
        const uint8_t *descriptor = interface->extra + position;
        uint8_t size = descriptor[0];
        if (size < 2U || position + size > interface->extra_length) break;
        if (descriptor[1] == HID_DESC_HID && size >= 9U) {
            uint8_t descriptors = descriptor[5];
            uint32_t entry = 6U;
            for (uint8_t i = 0; i < descriptors && entry + 3U <= size;
                 i++, entry += 3U) {
                if (descriptor[entry] == HID_DESC_REPORT)
                    return read16le(descriptor + entry + 1U);
            }
        }
        position += size;
    }
    return 0;
}

static hid_context_t *hid_allocate(void) {
    for (uint32_t i = 0; i < USBCLASS_MAX_HID; i++)
        if (!g_hid[i].active) return &g_hid[i];
    return NULL;
}

static void hid_release_keys(hid_context_t *context) {
    if (!context) return;
    for (uint32_t usage = 1U; usage < 256U; usage++) {
        if (context->keys[usage]) {
            kbd_inject_hid_usage((uint16_t)usage, false);
            context->keys[usage] = false;
        }
    }
}

static bool hid_attach(usb_device_t *device, usb_interface_t *interface) {
    hid_context_t *context;
    uint8_t report_descriptor[HID_MAX_REPORT_DESC];
    uint16_t descriptor_length;
    bool boot_keyboard;
    bool boot_mouse;
    bool report_ready = false;
    if (!device || !interface || interface->class_code != USB_CLASS_HID)
        return false;
    context = hid_allocate();
    if (!context) return false;
    kmemset(context, 0, sizeof(*context));
    context->device = device;
    context->interface = interface;
    context->input = usb_interface_endpoint(interface, USB_EP_INTERRUPT,
                                             true, 0);
    if (!context->input || !context->input->max_packet ||
        context->input->max_packet > HID_MAX_REPORT) return false;
    context->report_length = context->input->max_packet;
    if (context->report_length > HID_MAX_REPORT)
        context->report_length = HID_MAX_REPORT;
    boot_keyboard = interface->subclass == 1U && interface->protocol == 1U;
    boot_mouse = interface->subclass == 1U && interface->protocol == 2U;

    /* Prefer Report Protocol even on boot-capable devices. This preserves
     * extra buttons, wheels and non-boot layouts; Boot Protocol remains the
     * standards-defined recovery path when the report descriptor is absent
     * or unsupported by the compact parser. */
    descriptor_length = hid_report_descriptor_length(interface);
    if (descriptor_length && descriptor_length <= HID_MAX_REPORT_DESC) {
        kmemset(report_descriptor, 0, sizeof(report_descriptor));
        if (usb_control_transfer(device, USB_RT_IN_STD_INTERFACE,
             USB_REQ_GET_DESCRIPTOR, HID_DESC_REPORT << 8,
             interface->number, report_descriptor, descriptor_length)) {
            context->report_descriptor_length = descriptor_length;
            context->mode = HID_MODE_REPORT;
            report_ready = hid_parse_report_descriptor(
                context, report_descriptor, descriptor_length) &&
                (context->keyboard_capable || context->pointer_capable);
        }
    }
    if (report_ready) {
        if (boot_keyboard || boot_mouse)
            (void)usb_control_transfer(device, USB_RT_OUT_CLASS_INTERFACE,
                 HID_REQ_SET_PROTOCOL, 1U, interface->number, NULL, 0);
        (void)usb_control_transfer(device, USB_RT_OUT_CLASS_INTERFACE,
             HID_REQ_SET_IDLE, 0, interface->number, NULL, 0);
    } else if (boot_keyboard || boot_mouse) {
        context->field_count = 0;
        context->report_ids = false;
        context->keyboard_capable = boot_keyboard;
        context->pointer_capable = boot_mouse;
        kmemset(context->fields, 0, sizeof(context->fields));
        context->mode = boot_keyboard ? HID_MODE_BOOT_KEYBOARD
                                      : HID_MODE_BOOT_MOUSE;
        if (boot_keyboard) {
            if (context->input->max_packet < 8U) return false;
            context->report_length = 8U;
        } else if (context->report_length < 3U) return false;
        (void)usb_control_transfer(device, USB_RT_OUT_CLASS_INTERFACE,
             HID_REQ_SET_PROTOCOL, 0, interface->number, NULL, 0);
        (void)usb_control_transfer(device, USB_RT_OUT_CLASS_INTERFACE,
             HID_REQ_SET_IDLE, 0, interface->number, NULL, 0);
    } else return false;

    context->active = true;
    kprintf("  [USBCLASS] HID dev=%u intf=%u mode=%u ep=%x mps=%u fields=%u key=%u ptr=%u\n",
            device->id, interface->number, context->mode,
            context->input->address, context->input->max_packet,
            context->field_count, context->keyboard_capable,
            context->pointer_capable);
    return true;
}

static void hid_update_keys(hid_context_t *context, const bool current[256]) {
    for (uint32_t usage = 1U; usage < 256U; usage++) {
        if (current[usage] == context->keys[usage]) continue;
        kbd_inject_hid_usage((uint16_t)usage, current[usage]);
        context->keys[usage] = current[usage];
    }
}

static void hid_boot_keyboard(hid_context_t *context,
                              const uint8_t *report, uint32_t length) {
    bool current[256];
    if (length < 8U) return;
    kmemset(current, 0, sizeof(current));
    for (uint8_t bit = 0; bit < 8U; bit++)
        if (report[0] & (1U << bit)) current[0xE0U + bit] = true;
    for (uint8_t i = 2U; i < 8U; i++)
        if (report[i] > 3U) current[report[i]] = true;
    hid_update_keys(context, current);
}

static uint8_t hid_mouse_buttons(uint32_t bits) {
    uint8_t buttons = 0;
    if (bits & 1U) buttons |= MOUSE_LEFT_BUTTON;
    if (bits & 2U) buttons |= MOUSE_RIGHT_BUTTON;
    if (bits & 4U) buttons |= MOUSE_MIDDLE_BUTTON;
    if (bits & 8U) buttons |= MOUSE_BUTTON_4;
    if (bits & 16U) buttons |= MOUSE_BUTTON_5;
    return buttons;
}

static void hid_boot_mouse(hid_context_t *context,
                           const uint8_t *report, uint32_t length) {
    int32_t dx, dy, wheel = 0;
    uint8_t buttons;
    if (length < 3U) return;
    buttons = hid_mouse_buttons(report[0]);
    dx = (int8_t)report[1];
    dy = (int8_t)report[2];
    if (length >= 4U) wheel = (int8_t)report[3];
    context->mouse_buttons = buttons;
    mouse_inject_relative(dx, dy, wheel, buttons);
}

static void hid_report_input(hid_context_t *context,
                             const uint8_t *report, uint32_t length) {
    bool current_keys[256];
    uint8_t report_id = context->report_ids && length ? report[0] : 0U;
    uint32_t button_bits = 0;
    int32_t dx = 0, dy = 0, wheel = 0;
    bool keyboard_seen = false;
    bool pointer_seen = false;
    kmemset(current_keys, 0, sizeof(current_keys));

    for (uint32_t i = 0; i < context->field_count; i++) {
        hid_field_t *field = &context->fields[i];
        uint32_t raw;
        int32_t value;
        uint16_t usage;
        if (field->report_id != report_id) continue;
        raw = report_bits(report, length, field->bit_offset, field->bit_size);
        value = field->logical_min < 0 ? sign_extend_bits(raw, field->bit_size)
                                       : (int32_t)raw;
        usage = field->array ? (uint16_t)raw : field->usage;

        if (field->usage_page == HID_USAGE_PAGE_KEYBOARD) {
            keyboard_seen = true;
            if (field->array) {
                if (usage > 3U && usage < 256U) current_keys[usage] = true;
            } else if (usage < 256U && value) current_keys[usage] = true;
        } else if (field->application_page == HID_USAGE_PAGE_GENERIC_DESKTOP &&
                   (field->application_usage == HID_USAGE_MOUSE ||
                    field->application_usage == HID_USAGE_POINTER) &&
                   field->usage_page == HID_USAGE_PAGE_BUTTON &&
                   usage >= 1U && usage <= 32U && value) {
            button_bits |= 1U << (usage - 1U);
            pointer_seen = true;
        } else if (field->application_page == HID_USAGE_PAGE_GENERIC_DESKTOP &&
                   (field->application_usage == HID_USAGE_MOUSE ||
                    field->application_usage == HID_USAGE_POINTER) &&
                   field->usage_page == HID_USAGE_PAGE_GENERIC_DESKTOP) {
            int32_t delta = value;
            if (!field->relative) {
                if (!field->seen) delta = 0;
                else delta = value - field->last_value;
                field->last_value = value;
                field->seen = true;
            }
            if (usage == HID_USAGE_X) { dx += delta; pointer_seen = true; }
            else if (usage == HID_USAGE_Y) { dy += delta; pointer_seen = true; }
            else if (usage == HID_USAGE_WHEEL) { wheel += delta; pointer_seen = true; }
        } else if (field->usage_page == HID_USAGE_PAGE_CONSUMER) {
            /* Parsed and retained by the generic report engine. BlesKernOS
             * does not yet expose multimedia-key events in its keyboard ABI. */
        }
    }
    if (keyboard_seen) hid_update_keys(context, current_keys);
    if (pointer_seen) {
        context->mouse_buttons = hid_mouse_buttons(button_bits);
        mouse_inject_relative(dx, dy, wheel, context->mouse_buttons);
    }
}

static void hid_poll(hid_context_t *context) {
    uint8_t report[HID_MAX_REPORT];
    uint32_t actual = 0;
    if (!context || !context->active || !context->device->connected) return;
    kmemset(report, 0, sizeof(report));
    if (!usb_endpoint_transfer(context->device, context->input, report,
                               context->report_length, &actual, 2U)) {
        if (++context->failures > 100U) {
            (void)usb_clear_endpoint_halt(context->device, context->input);
            context->failures = 0;
        }
        return;
    }
    context->failures = 0;
    if (!actual) return;
    if (context->mode == HID_MODE_BOOT_KEYBOARD)
        hid_boot_keyboard(context, report, actual);
    else if (context->mode == HID_MODE_BOOT_MOUSE)
        hid_boot_mouse(context, report, actual);
    else hid_report_input(context, report, actual);
}

static hub_context_t *hub_allocate(void) {
    for (uint32_t i = 0; i < USBCLASS_MAX_HUB; i++)
        if (!g_hub[i].active) return &g_hub[i];
    return NULL;
}

static bool hub_get_port_status(hub_context_t *hub, uint8_t port,
                                uint16_t *status, uint16_t *change) {
    uint8_t data[4];
    if (!hub || !port) return false;
    if (!usb_control_transfer(hub->device, USB_RT_IN_CLASS_OTHER,
        0, 0, port, data, sizeof(data))) return false;
    if (status) *status = read16le(data);
    if (change) *change = read16le(data + 2U);
    return true;
}

static bool hub_set_port_feature(hub_context_t *hub, uint8_t port,
                                 uint16_t feature) {
    return usb_control_transfer(hub->device, USB_RT_OUT_CLASS_OTHER,
        USB_REQ_SET_FEATURE, feature, port, NULL, 0);
}

static bool hub_clear_port_feature(hub_context_t *hub, uint8_t port,
                                   uint16_t feature) {
    return usb_control_transfer(hub->device, USB_RT_OUT_CLASS_OTHER,
        USB_REQ_CLEAR_FEATURE, feature, port, NULL, 0);
}

static void hub_clear_changes(hub_context_t *hub, uint8_t port,
                              uint16_t change) {
    if (change & (1U << 0))
        (void)hub_clear_port_feature(hub, port, HUB_C_PORT_CONNECTION);
    if (change & (1U << 1))
        (void)hub_clear_port_feature(hub, port, HUB_C_PORT_ENABLE);
    if (change & (1U << 2))
        (void)hub_clear_port_feature(hub, port, HUB_C_PORT_SUSPEND);
    if (change & (1U << 3))
        (void)hub_clear_port_feature(hub, port, HUB_C_PORT_OVERCURRENT);
    if (change & (1U << 4))
        (void)hub_clear_port_feature(hub, port, HUB_C_PORT_RESET);
}

static bool hub_reset_and_enumerate(hub_context_t *hub, uint8_t port) {
    uint16_t status = 0, change = 0;
    sleep_ms(100U); /* connection debounce */
    if (!hub_get_port_status(hub, port, &status, &change) ||
        !(status & HUB_STATUS_CONNECTION)) return false;
    if (!hub_set_port_feature(hub, port, HUB_PORT_RESET)) return false;
    for (uint32_t wait = 0; wait < 20U; wait++) {
        sleep_ms(10U);
        if (!hub_get_port_status(hub, port, &status, &change)) continue;
        if ((status & HUB_STATUS_ENABLE) && !(status & HUB_STATUS_RESET)) break;
    }
    hub_clear_changes(hub, port, change);
    if (!(status & HUB_STATUS_CONNECTION) || !(status & HUB_STATUS_ENABLE))
        return false;
    return usb_enumerate_hub_port(hub->device, port,
            (status & HUB_STATUS_LOW_SPEED) ? USB_SPEED_LOW : USB_SPEED_FULL);
}

static void hub_process_port(hub_context_t *hub, uint8_t port) {
    uint16_t status = 0, change = 0;
    if (!hub_get_port_status(hub, port, &status, &change)) return;
    if (status & HUB_STATUS_OVERCURRENT) {
        usb_disconnect_hub_port(hub->device, port);
        hub_clear_changes(hub, port, change);
        return;
    }
    if (change & 1U) {
        if (status & HUB_STATUS_CONNECTION)
            (void)hub_reset_and_enumerate(hub, port);
        else usb_disconnect_hub_port(hub->device, port);
    } else if ((status & HUB_STATUS_CONNECTION) &&
               !(status & HUB_STATUS_ENABLE)) {
        (void)hub_reset_and_enumerate(hub, port);
    }
    hub_clear_changes(hub, port, change);
}

static bool hub_attach(usb_device_t *device, usb_interface_t *interface) {
    hub_context_t *hub;
    uint8_t descriptor[64];
    uint16_t delay;
    if (!device || !interface || interface->class_code != USB_CLASS_HUB)
        return false;
    hub = hub_allocate();
    if (!hub) return false;
    kmemset(hub, 0, sizeof(*hub));
    kmemset(descriptor, 0, sizeof(descriptor));
    if (!usb_control_transfer(device, USB_RT_IN_CLASS_DEVICE,
         USB_REQ_GET_DESCRIPTOR, HUB_DESC_TYPE << 8, 0,
         descriptor, sizeof(descriptor))) return false;
    if (descriptor[0] < 7U || descriptor[1] != HUB_DESC_TYPE ||
        !descriptor[2] || descriptor[2] > 31U) return false;
    hub->device = device;
    hub->interface = interface;
    hub->ports = descriptor[2];
    hub->characteristics = read16le(descriptor + 3U);
    hub->power_good_ms = (uint16_t)descriptor[5] * 2U;
    hub->status_endpoint = usb_interface_endpoint(interface,
        USB_EP_INTERRUPT, true, 0);
    for (uint8_t port = 1U; port <= hub->ports; port++)
        (void)hub_set_port_feature(hub, port, HUB_PORT_POWER);
    delay = hub->power_good_ms;
    if (delay < 20U) delay = 20U;
    sleep_ms(delay);
    hub->active = true;
    for (uint8_t port = 1U; port <= hub->ports; port++)
        hub_process_port(hub, port);
    hub->last_full_poll = pit_get_ticks();
    kprintf("  [USBCLASS] hub dev=%u ports=%u power-good=%ums ep=%x\n",
            device->id, hub->ports, hub->power_good_ms,
            hub->status_endpoint ? hub->status_endpoint->address : 0U);
    return true;
}

static void hub_poll(hub_context_t *hub) {
    uint8_t bitmap[8];
    uint32_t actual = 0;
    bool full_poll;
    if (!hub || !hub->active || !hub->device->connected) return;
    full_poll = elapsed_ms(hub->last_full_poll) >= 500U;
    if (hub->status_endpoint) {
        uint32_t bytes = ((uint32_t)hub->ports + 1U + 7U) / 8U;
        if (bytes > sizeof(bitmap)) bytes = sizeof(bitmap);
        kmemset(bitmap, 0, sizeof(bitmap));
        if (usb_endpoint_transfer(hub->device, hub->status_endpoint,
                                  bitmap, bytes, &actual, 2U)) {
            hub->failures = 0;
            for (uint8_t port = 1U; port <= hub->ports; port++) {
                if ((uint32_t)(port >> 3) < actual &&
                    (bitmap[port >> 3] & (1U << (port & 7U))))
                    hub_process_port(hub, port);
            }
        } else if (++hub->failures > 100U) {
            (void)usb_clear_endpoint_halt(hub->device,
                                          hub->status_endpoint);
            hub->failures = 0;
        }
    }
    if (full_poll) {
        for (uint8_t port = 1U; port <= hub->ports; port++)
            hub_process_port(hub, port);
        hub->last_full_poll = pit_get_ticks();
    }
}

static printer_context_t *printer_allocate(void) {
    for (uint32_t i = 0; i < USBCLASS_MAX_PRINTER; i++)
        if (!g_printer[i].active) return &g_printer[i];
    return NULL;
}

static bool printer_get_status(printer_context_t *printer) {
    uint8_t status = 0;
    if (!printer || !printer->active) return false;
    if (!usb_control_transfer(printer->device, USB_RT_IN_CLASS_INTERFACE,
        PRINTER_REQ_GET_PORT_STATUS, 0, printer->interface->number,
        &status, 1U)) return false;
    printer->status = status;
    return true;
}

static void printer_get_device_id(printer_context_t *printer) {
    uint8_t buffer[192];
    uint16_t declared;
    uint16_t index;
    if (!printer) return;
    kmemset(buffer, 0, sizeof(buffer));
    index = (uint16_t)(((uint16_t)printer->interface->number << 8) |
                       printer->interface->alternate);
    if (!usb_control_transfer(printer->device, USB_RT_IN_CLASS_INTERFACE,
        PRINTER_REQ_GET_DEVICE_ID, 0, index, buffer, sizeof(buffer))) return;
    declared = ((uint16_t)buffer[0] << 8) | buffer[1];
    if (declared < 2U) return;
    declared -= 2U;
    if (declared >= sizeof(printer->device_id))
        declared = sizeof(printer->device_id) - 1U;
    kmemcpy(printer->device_id, buffer + 2U, declared);
    printer->device_id[declared] = '\0';
}

static bool printer_attach(usb_device_t *device,
                           usb_interface_t *interface) {
    printer_context_t *printer;
    usb_interface_t *selected = interface;
    usb_endpoint_t *bulk_out;
    if (!device || !interface || interface->class_code != USB_CLASS_PRINTER ||
        interface->subclass != 1U) return false;
    /* Prefer a bidirectional alternate setting when one exists. */
    for (uint32_t i = 0; i < device->interface_count; i++) {
        usb_interface_t *candidate = &device->interfaces[i];
        if (candidate->number == interface->number &&
            candidate->class_code == USB_CLASS_PRINTER &&
            candidate->subclass == 1U && candidate->protocol == 2U) {
            selected = candidate;
            break;
        }
    }
    if (selected->protocol != 1U && selected->protocol != 2U) {
        kprintf("  [USBCLASS] printer dev=%u protocolo %u requiere IEEE 1284.4\n",
                device->id, selected->protocol);
        return false;
    }
    if (selected->alternate != interface->alternate &&
        !usb_set_interface(device, selected->number, selected->alternate))
        return false;
    bulk_out = usb_interface_endpoint(selected, USB_EP_BULK, false, 0);
    if (!bulk_out) return false;
    printer = printer_allocate();
    if (!printer) return false;
    kmemset(printer, 0, sizeof(*printer));
    printer->device = device;
    printer->interface = selected;
    printer->bulk_out = bulk_out;
    printer->bulk_in = usb_interface_endpoint(selected, USB_EP_BULK, true, 0);
    printer->protocol = selected->protocol;
    printer->status = 0x18U; /* benign default: selected and no error */
    printer->active = true;
    printer_get_device_id(printer);
    (void)printer_get_status(printer);
    kprintf("  [USBCLASS] printer dev=%u protocol=%u out=%x in=%x id=%s\n",
            device->id, printer->protocol, printer->bulk_out->address,
            printer->bulk_in ? printer->bulk_in->address : 0U,
            printer->device_id[0] ? printer->device_id : "(sin IEEE1284 ID)");
    return true;
}

static uint32_t printer_provider_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < USBCLASS_MAX_PRINTER; i++)
        if (g_printer[i].active && g_printer[i].device->connected) count++;
    return count;
}

static printer_context_t *printer_provider_at(uint32_t index) {
    for (uint32_t i = 0; i < USBCLASS_MAX_PRINTER; i++) {
        if (!g_printer[i].active || !g_printer[i].device->connected) continue;
        if (!index--) return &g_printer[i];
    }
    return NULL;
}

static bool printer_provider_info(uint32_t index, lpt_port_info_t *info) {
    printer_context_t *printer = printer_provider_at(index);
    if (!printer || !info) return false;
    (void)printer_get_status(printer);
    kmemset(info, 0, sizeof(*info));
    info->name[0] = 'U'; info->name[1] = 'S'; info->name[2] = 'B';
    info->name[3] = 'P'; info->name[4] = 'R'; info->name[5] = 'N';
    info->name[6] = (char)('1' + (printer - g_printer));
    info->name[7] = '\0';
    info->raw_status = printer->status;
    info->present = true;
    info->busy = false;
    info->selected = (printer->status & (1U << 4)) != 0U;
    info->paper_out = (printer->status & (1U << 5)) != 0U;
    info->error = (printer->status & (1U << 3)) == 0U;
    info->acknowledged = true;
    info->virtual_port = true;
    return true;
}

static int32_t printer_provider_write(uint32_t index, const void *data,
                                      uint32_t length,
                                      uint32_t idle_timeout_ms) {
    printer_context_t *printer = printer_provider_at(index);
    uint32_t actual = 0;
    uint32_t timeout = idle_timeout_ms ? idle_timeout_ms : 5000U;
    if (!printer || (!data && length)) return -2;
    if (!length) return 0;
    if (!printer_get_status(printer)) return -4;
    if (!(printer->status & (1U << 4)) || (printer->status & (1U << 5)) ||
        !(printer->status & (1U << 3))) return -4;
    if (usb_endpoint_transfer(printer->device, printer->bulk_out,
                              (void *)data, length, &actual, timeout) &&
        actual == length) return (int32_t)length;
    (void)usb_clear_endpoint_halt(printer->device, printer->bulk_out);
    actual = 0;
    if (usb_endpoint_transfer(printer->device, printer->bulk_out,
                              (void *)data, length, &actual, timeout) &&
        actual == length) return (int32_t)length;
    (void)usb_control_transfer(printer->device, USB_RT_OUT_CLASS_INTERFACE,
        PRINTER_REQ_SOFT_RESET, 0, printer->interface->number, NULL, 0);
    printer->bulk_out->toggle = 0;
    if (printer->bulk_in) printer->bulk_in->toggle = 0;
    return -5;
}

static bool usbclass_probe(usb_device_t *device,
                           usb_interface_t *interface) {
    if (!device || !interface) return false;
    if (interface->class_code == USB_CLASS_HID)
        return hid_attach(device, interface);
    if (interface->class_code == USB_CLASS_HUB)
        return hub_attach(device, interface);
    if (interface->class_code == USB_CLASS_PRINTER)
        return printer_attach(device, interface);
    return false;
}

static void usbclass_disconnect(usb_device_t *device,
                                usb_interface_t *interface) {
    bool any_mouse = false;
    for (uint32_t i = 0; i < USBCLASS_MAX_HID; i++) {
        hid_context_t *context = &g_hid[i];
        if (context->active && context->device == device &&
            context->interface == interface) {
            hid_release_keys(context);
            context->active = false;
        }
    }
    for (uint32_t i = 0; i < USBCLASS_MAX_HUB; i++) {
        if (g_hub[i].active && g_hub[i].device == device &&
            g_hub[i].interface == interface) g_hub[i].active = false;
    }
    for (uint32_t i = 0; i < USBCLASS_MAX_PRINTER; i++) {
        if (g_printer[i].active && g_printer[i].device == device)
            g_printer[i].active = false;
    }
    for (uint32_t i = 0; i < USBCLASS_MAX_HID; i++) {
        if (g_hid[i].active && g_hid[i].pointer_capable) {
            any_mouse = true;
            break;
        }
    }
    if (!any_mouse) mouse_inject_disconnect();
}

static void usbclass_poll_task(void *argument UNUSED) {
    while (g_running) {
        for (uint32_t i = 0; i < USBCLASS_MAX_HID; i++) hid_poll(&g_hid[i]);
        for (uint32_t i = 0; i < USBCLASS_MAX_HUB; i++) hub_poll(&g_hub[i]);
        /* UHCI interrupt endpoints no necesitan un loop de 300 Hz. 10 ms es
         * la cadencia USB HID habitual y sigue siendo imperceptible al input. */
        sleep_ms(10U);
    }
}

static bool usbclass_init(void) {
    static const lpt_virtual_provider_t printer_provider = {
        printer_provider_count,
        printer_provider_info,
        printer_provider_write
    };
    static const usb_class_driver_t class_driver = {
        BK_USB_CORE_ABI_VERSION,
        sizeof(usb_class_driver_t),
        "usbclass",
        usbclass_probe,
        usbclass_disconnect
    };
    kmemset(g_hid, 0, sizeof(g_hid));
    kmemset(g_hub, 0, sizeof(g_hub));
    kmemset(g_printer, 0, sizeof(g_printer));
    if (!lpt_register_virtual_provider(&printer_provider)) return false;
    if (!usb_class_register(&class_driver)) {
        (void)lpt_unregister_virtual_provider(&printer_provider);
        return false;
    }
    g_running = true;
    if (task_create("usbclass-poll", usbclass_poll_task, NULL) < 0) {
        g_running = false;
        (void)usb_class_unregister(&class_driver);
        (void)lpt_unregister_virtual_provider(&printer_provider);
        return false;
    }
    kprintf("  [USBCLASS] HID boot/report, hubs USB 1.1 y printers cargados\n");
    return true;
}

static void usbclass_shutdown(void) {
    g_running = false;
    for (uint32_t i = 0; i < USBCLASS_MAX_HID; i++) {
        if (g_hid[i].active) hid_release_keys(&g_hid[i]);
        g_hid[i].active = false;
    }
    mouse_inject_disconnect();
}

const bk_driver_module_t *bleskernos_driver_query(void) {
    static const bk_driver_module_t module = {
        BK_DRIVER_ABI_VERSION,
        sizeof(bk_driver_module_t),
        "usbclass",
        "USB HID boot/report, hubs y printer class sobre UHCI",
        usbclass_init,
        usbclass_shutdown
    };
    return &module;
}
