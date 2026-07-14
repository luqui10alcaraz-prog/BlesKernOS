#include "control_common.h"
#include "kernel/include/api.h"

#define DEVMGR_WINDOW_W        620
#define DEVMGR_WINDOW_H        430
#define DEVMGR_ROW_H            22
#define DEVMGR_MAX_ITEMS        (PCI_MAX_DEVICES + BLOCK_MAX_DEVICES + 16)
#define DEVMGR_DBLCLICK_TICKS  500
#define DEVMGR_ICON_SIZE        20

typedef enum {
    DEVMGR_KIND_BLOCK = 1,
    DEVMGR_KIND_PCI,
    DEVMGR_KIND_DISPLAY,
    DEVMGR_KIND_AUDIO,
    DEVMGR_KIND_INPUT,
    DEVMGR_KIND_RTC,
    DEVMGR_KIND_MEMORY,
    DEVMGR_KIND_KERNEL,
} devmgr_kind_t;

typedef enum {
    DEVMGR_SECTION_COMPUTER = 0,
    DEVMGR_SECTION_SYSTEM,
    DEVMGR_SECTION_MEMORY,
    DEVMGR_SECTION_BLOCK,
    DEVMGR_SECTION_DISPLAY,
    DEVMGR_SECTION_AUDIO,
    DEVMGR_SECTION_INPUT,
    DEVMGR_SECTION_RTC,
    DEVMGR_SECTION_PCI,
    DEVMGR_SECTION_COUNT,
} devmgr_section_t;

typedef enum {
    DEVMGR_ROW_ROOT = 0,
    DEVMGR_ROW_SECTION,
    DEVMGR_ROW_DEVICE,
} devmgr_row_type_t;

typedef enum {
    DEVMGR_ICON_COMPUTER = 0,
    DEVMGR_ICON_SYSTEM,
    DEVMGR_ICON_MEMORY,
    DEVMGR_ICON_BLOCK,
    DEVMGR_ICON_DISPLAY,
    DEVMGR_ICON_AUDIO,
    DEVMGR_ICON_INPUT,
    DEVMGR_ICON_RTC,
    DEVMGR_ICON_PCI,
    DEVMGR_ICON_COUNT,
} devmgr_icon_t;

typedef struct {
    devmgr_kind_t kind;
    uint32_t index;
    char name[64];
    char driver[32];
    char status[18];
    char detail[128];
} devmgr_item_t;

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    devmgr_item_t items[DEVMGR_MAX_ITEMS];
    uint32_t count;
    int selected;
    int hover;
    int pressed;
    uint32_t scroll;
    gui_scrollbar_drag_t scrollbar_drag;
    int last_clicked;
    uint32_t last_click_tick;
    uint32_t next_refresh;
    bool truncated;
    uint32_t expanded_mask;
    char status[96];
    uint32_t *icons[DEVMGR_ICON_COUNT];
} devmgr_state_t;

typedef struct {
    devmgr_item_t item;
    int active_tab;
    system_memory_info_t memory;
    gfx_info_t gfx;
    mouse_state_t mouse;
    rtc_datetime_t now;
    bool rtc_ok;
    uint32_t *icon;
} devmgr_detail_t;

static const char *devmgr_kind_text(devmgr_kind_t kind) {
    switch (kind) {
        case DEVMGR_KIND_BLOCK: return "Almacenamiento";
        case DEVMGR_KIND_PCI: return "PCI";
        case DEVMGR_KIND_DISPLAY: return "Pantalla";
        case DEVMGR_KIND_AUDIO: return "Sonido";
        case DEVMGR_KIND_INPUT: return "Entrada";
        case DEVMGR_KIND_RTC: return "Reloj";
        case DEVMGR_KIND_MEMORY: return "Memoria";
        case DEVMGR_KIND_KERNEL: return "Sistema";
        default: return "Dispositivo";
    }
}

static devmgr_section_t devmgr_section_for_kind(devmgr_kind_t kind) {
    switch (kind) {
        case DEVMGR_KIND_KERNEL: return DEVMGR_SECTION_SYSTEM;
        case DEVMGR_KIND_MEMORY: return DEVMGR_SECTION_MEMORY;
        case DEVMGR_KIND_BLOCK: return DEVMGR_SECTION_BLOCK;
        case DEVMGR_KIND_DISPLAY: return DEVMGR_SECTION_DISPLAY;
        case DEVMGR_KIND_AUDIO: return DEVMGR_SECTION_AUDIO;
        case DEVMGR_KIND_INPUT: return DEVMGR_SECTION_INPUT;
        case DEVMGR_KIND_RTC: return DEVMGR_SECTION_RTC;
        case DEVMGR_KIND_PCI: return DEVMGR_SECTION_PCI;
        default: return DEVMGR_SECTION_SYSTEM;
    }
}

static const char *devmgr_section_text(devmgr_section_t section) {
    switch (section) {
        case DEVMGR_SECTION_COMPUTER: return "Equipo";
        case DEVMGR_SECTION_SYSTEM: return "Dispositivos del sistema";
        case DEVMGR_SECTION_MEMORY: return "Memoria";
        case DEVMGR_SECTION_BLOCK: return "Unidades de disco";
        case DEVMGR_SECTION_DISPLAY: return "Adaptadores de pantalla";
        case DEVMGR_SECTION_AUDIO: return "Controladores de sonido";
        case DEVMGR_SECTION_INPUT: return "Teclado y mouse";
        case DEVMGR_SECTION_RTC: return "Reloj del sistema";
        case DEVMGR_SECTION_PCI: return "Dispositivos PCI";
        default: return "Dispositivos";
    }
}

static devmgr_icon_t devmgr_icon_for_kind(devmgr_kind_t kind) {
    switch (kind) {
        case DEVMGR_KIND_KERNEL: return DEVMGR_ICON_SYSTEM;
        case DEVMGR_KIND_MEMORY: return DEVMGR_ICON_MEMORY;
        case DEVMGR_KIND_BLOCK: return DEVMGR_ICON_BLOCK;
        case DEVMGR_KIND_DISPLAY: return DEVMGR_ICON_DISPLAY;
        case DEVMGR_KIND_AUDIO: return DEVMGR_ICON_AUDIO;
        case DEVMGR_KIND_INPUT: return DEVMGR_ICON_INPUT;
        case DEVMGR_KIND_RTC: return DEVMGR_ICON_RTC;
        case DEVMGR_KIND_PCI: return DEVMGR_ICON_PCI;
        default: return DEVMGR_ICON_SYSTEM;
    }
}

static uint32_t devmgr_section_bit(devmgr_section_t section) {
    return 1U << (uint32_t)section;
}

static bool devmgr_section_expanded(const devmgr_state_t *st,
                                    devmgr_section_t section) {
    return st && (st->expanded_mask & devmgr_section_bit(section)) != 0;
}

static uint32_t devmgr_count_in_section(const devmgr_state_t *st,
                                        devmgr_section_t section) {
    uint32_t count = 0;
    if (!st) return 0;
    for (uint32_t i = 0; i < st->count; i++) {
        if (devmgr_section_for_kind(st->items[i].kind) == section) count++;
    }
    return count;
}

static void devmgr_copy(char *dst, size_t len, const char *src) {
    if (!dst || !len) return;
    if (!src) src = "";
    bk_runtime_strncpy(dst, src, len - 1);
    dst[len - 1] = '\0';
}

static const bk_loaded_driver_t *devmgr_loaded_driver(const char *name) {
    if (!name) return NULL;
    for (uint32_t i = 0; i < bk_device_driver_count(); i++) {
        const bk_loaded_driver_t *driver = bk_device_driver_at(i);
        if (driver && bk_runtime_strcmp(driver->name, name) == 0) return driver;
    }
    return NULL;
}

static const char *devmgr_driver_filename(const char *name,
                                          const char *fallback) {
    const bk_loaded_driver_t *driver = devmgr_loaded_driver(name);
    const char *filename;

    if (!driver || !driver->path[0]) return fallback;
    filename = driver->path;
    for (const char *p = driver->path; *p; p++) {
        if (*p == '/' || *p == '\\') filename = p + 1;
    }
    return filename[0] ? filename : fallback;
}

typedef struct {
    uint16_t vendor;
    uint16_t device;
    const char *name;
    const char *driver;
} devmgr_pci_id_t;

typedef struct {
    uint16_t vendor;
    const char *name;
} devmgr_pci_vendor_t;

/* Catalogo separado para poder ampliarlo sin mezclar datos con la GUI. */
#include "pci_device_db.h"
#include "pci_vendor_db.h"

static const char *devmgr_pci_vendor(uint16_t vendor) {
    uint32_t left = 0;
    uint32_t right = sizeof(g_devmgr_pci_vendors) /
                     sizeof(g_devmgr_pci_vendors[0]);
    while (left < right) {
        uint32_t middle = left + (right - left) / 2U;
        uint16_t candidate = g_devmgr_pci_vendors[middle].vendor;
        if (candidate == vendor) return g_devmgr_pci_vendors[middle].name;
        if (candidate < vendor) left = middle + 1U;
        else right = middle;
    }
    return "PCI";
}

static const devmgr_pci_id_t *devmgr_pci_lookup(const pci_device_t *dev) {
    if (!dev) return NULL;
    for (uint32_t i = 0;
         i < sizeof(g_devmgr_pci_ids) / sizeof(g_devmgr_pci_ids[0]); i++) {
        if (g_devmgr_pci_ids[i].vendor == dev->vendor_id &&
            g_devmgr_pci_ids[i].device == dev->device_id)
            return &g_devmgr_pci_ids[i];
    }
    return NULL;
}

static void devmgr_pci_identity(const pci_device_t *dev,
                                char *name, size_t name_len,
                                char *driver, size_t driver_len) {
    const devmgr_pci_id_t *known = devmgr_pci_lookup(dev);
    const char *class_name;
    const char *generic_driver = "pci";

    if (!dev || !name || !driver) return;
    if (known) {
        snprintf(name, name_len, "%s", known->name);
        snprintf(driver, driver_len, "%s", known->driver);
        return;
    }

    class_name = bk_device_pci_class_name(dev->class_code, dev->subclass);
    snprintf(name, name_len, "%s %s [%04x:%04x]",
             devmgr_pci_vendor(dev->vendor_id), class_name,
             dev->vendor_id, dev->device_id);

    if (dev->class_code == 0x01 && dev->subclass == 0x01)
        generic_driver = "ata";
    else if (dev->class_code == 0x0C && dev->subclass == 0x03 &&
             dev->prog_if == 0x00)
        generic_driver = "usb_uhci";
    else if (dev->class_code == 0x03)
        generic_driver = "gfx/vesa";
    else if (dev->class_code == 0x06)
        generic_driver = "pci-bridge";
    snprintf(driver, driver_len, "%s", generic_driver);
}

static devmgr_item_t *devmgr_add(devmgr_state_t *st, devmgr_kind_t kind,
                                 uint32_t index, const char *name,
                                 const char *driver,
                                 const char *status,
                                 const char *detail) {
    devmgr_item_t *item;

    if (!st) return NULL;
    if (st->count >= DEVMGR_MAX_ITEMS) {
        st->truncated = true;
        return NULL;
    }
    item = &st->items[st->count++];
    bk_runtime_memset(item, 0, sizeof(*item));
    item->kind = kind;
    item->index = index;
    devmgr_copy(item->name, sizeof(item->name), name);
    devmgr_copy(item->driver, sizeof(item->driver), driver);
    devmgr_copy(item->status, sizeof(item->status), status);
    devmgr_copy(item->detail, sizeof(item->detail), detail);
    return item;
}

static bool devmgr_visible_row_info(const devmgr_state_t *st,
                                    uint32_t visible_index,
                                    devmgr_row_type_t *type,
                                    devmgr_section_t *section,
                                    int *item_index) {
    uint32_t row = 0;

    if (!st) return false;
    if (visible_index == row++) {
        if (type) *type = DEVMGR_ROW_ROOT;
        if (section) *section = DEVMGR_SECTION_COMPUTER;
        if (item_index) *item_index = -1;
        return true;
    }

    for (uint32_t s = DEVMGR_SECTION_SYSTEM; s < DEVMGR_SECTION_COUNT; s++) {
        devmgr_section_t sec = (devmgr_section_t)s;
        if (!devmgr_count_in_section(st, sec)) continue;
        if (visible_index == row++) {
            if (type) *type = DEVMGR_ROW_SECTION;
            if (section) *section = sec;
            if (item_index) *item_index = -1;
            return true;
        }
        if (!devmgr_section_expanded(st, sec)) continue;
        for (uint32_t i = 0; i < st->count; i++) {
            if (devmgr_section_for_kind(st->items[i].kind) != sec) continue;
            if (visible_index == row++) {
                if (type) *type = DEVMGR_ROW_DEVICE;
                if (section) *section = sec;
                if (item_index) *item_index = (int)i;
                return true;
            }
        }
    }
    return false;
}

static uint32_t devmgr_visible_count(const devmgr_state_t *st) {
    uint32_t count = 0;

    if (!st) return 0;
    count++;
    for (uint32_t s = DEVMGR_SECTION_SYSTEM; s < DEVMGR_SECTION_COUNT; s++) {
        devmgr_section_t sec = (devmgr_section_t)s;
        uint32_t section_items = devmgr_count_in_section(st, sec);
        if (!section_items) continue;
        count++;
        if (devmgr_section_expanded(st, sec)) count += section_items;
    }
    return count;
}

static uint32_t devmgr_visible_rows(const devmgr_state_t *st);
static void devmgr_clamp_scroll(devmgr_state_t *st);
static void devmgr_ensure_selected_visible(devmgr_state_t *st);

static void devmgr_refresh(devmgr_state_t *st) {
    system_memory_info_t mem;
    gfx_info_t gfx;
    mouse_state_t mouse;
    rtc_datetime_t now;
    char detail[128];
    uint32_t block_devices;
    uint32_t pci_devices;
    bool gfx_ok;

    if (!st) return;
    st->count = 0;
    st->truncated = false;
    gfx_ok = bk_gfx_info(&gfx);

    snprintf(detail, sizeof(detail), "API v%u, %u procesos",
             bk_sys_api_version(), bk_proc_count());
    (void)devmgr_add(st, DEVMGR_KIND_KERNEL, 0, "Kernel BlesKernOS",
                     "kernel", "OK", detail);

    if (bk_sys_memory_info(&mem)) {
        snprintf(detail, sizeof(detail), "%u KB total, %u KB libres",
                 (uint32_t)(mem.total_bytes / 1024U),
                 (uint32_t)(mem.free_bytes / 1024U));
        (void)devmgr_add(st, DEVMGR_KIND_MEMORY, 0, "Memoria fisica",
                         "memory", "OK", detail);
    }

    block_devices = bk_device_block_count();
    for (uint32_t i = 0; i < block_devices; i++) {
        block_device_t *dev = bk_device_block_at(i);
        const char *block_driver;
        if (!dev) continue;
        block_driver = bk_device_block_type_name(dev->type);
        if (dev->type == BLOCK_DEVICE_ATAPI &&
            devmgr_loaded_driver("iso9660"))
            block_driver = "ATA + ISO9660.DVR";
        snprintf(detail, sizeof(detail), "%s, %u sectores de %u bytes%s",
                 bk_device_block_type_name(dev->type), dev->sector_count,
                 dev->sector_size, dev->read_only ? ", solo lectura" : "");
        (void)devmgr_add(st, DEVMGR_KIND_BLOCK, i, dev->name,
                         block_driver,
                         dev->read_only ? "RO" : "OK", detail);
    }

    pci_devices = bk_device_pci_count();
    for (uint32_t i = 0; i < pci_devices; i++) {
        const pci_device_t *dev = bk_device_pci_at(i);
        char name[64];
        char driver[32];
        if (!dev) continue;
        devmgr_pci_identity(dev, name, sizeof(name), driver, sizeof(driver));
        if (bk_runtime_strcmp(driver, "ac97") == 0 && devmgr_loaded_driver("ac97"))
            devmgr_copy(driver, sizeof(driver),
                        devmgr_driver_filename("ac97", "AC97.DVR"));
        if (dev->class_code == 0x03 && gfx_ok &&
            gfx.mode == GFX_MODE_VESA_LFB && devmgr_loaded_driver("vesa"))
            devmgr_copy(driver, sizeof(driver),
                        devmgr_driver_filename("vesa", "VESA.DVR"));
        snprintf(detail, sizeof(detail),
                 "PCI %02x:%02x.%u  ID %04x:%04x  clase %02x:%02x.%02x rev %02x",
                 dev->bus, dev->slot, dev->function,
                 dev->vendor_id, dev->device_id, dev->class_code,
                 dev->subclass, dev->prog_if, dev->revision_id);
        (void)devmgr_add(st, DEVMGR_KIND_PCI, i, name,
                         driver, "DETECTADO", detail);
    }

    if (gfx_ok) {
        snprintf(detail, sizeof(detail), "%ux%ux%u",
                 gfx.width, gfx.height, gfx.bpp);
        (void)devmgr_add(st, DEVMGR_KIND_DISPLAY, 0,
                         "Adaptador grafico",
                         devmgr_driver_filename("vesa", "gfx/vga"),
                         "OK", detail);
    }

    snprintf(detail, sizeof(detail), "%s",
             bk_sound_pcm_available()
                ? (bk_sound_pcm_name() ? bk_sound_pcm_name() : "PCM disponible")
                : "PCM no disponible");
    (void)devmgr_add(st, DEVMGR_KIND_AUDIO, 0, "Audio PCM",
                     bk_sound_pcm_name() &&
                     bk_runtime_strncmp(bk_sound_pcm_name(), "Intel AC'97", 11) == 0
                        ? devmgr_driver_filename("ac97", "AC97.DVR")
                        : devmgr_driver_filename("sb16", "sound_core"),
                     bk_sound_pcm_available() ? "OK" : "FALLO", detail);

    (void)bk_input_mouse(&mouse);
    snprintf(detail, sizeof(detail), "id=%u pkt=%u irq=%u pos=%i,%i",
             (uint32_t)mouse.device_id, (uint32_t)mouse.packet_size,
             mouse.irq_count, (int)mouse.x, (int)mouse.y);
    (void)devmgr_add(st, DEVMGR_KIND_INPUT, 0, "Mouse PS/2",
                     devmgr_driver_filename("ps2-mouse", "mouse_core"),
                     mouse.present ? "OK" : "FALLO", detail);
    (void)devmgr_add(st, DEVMGR_KIND_INPUT, 1, "Teclado PS/2",
                     "ps2kbd", "OK", "Eventos entregados a la GUI");

    if (bk_time_datetime(&now)) {
        snprintf(detail, sizeof(detail), "%u-%02u-%02u %02u:%02u:%02u",
                 now.date.year, now.date.month, now.date.day,
                 now.time.hour, now.time.minute, now.time.second);
        (void)devmgr_add(st, DEVMGR_KIND_RTC, 0, "RTC CMOS",
                         devmgr_driver_filename("cmos-rtc", "rtc_core"),
                         "OK", detail);
    } else {
        (void)devmgr_add(st, DEVMGR_KIND_RTC, 0, "RTC CMOS",
                         devmgr_driver_filename("cmos-rtc", "rtc_core"),
                         "FALLO", "No responde");
    }

    if (st->selected >= (int)devmgr_visible_count(st)) st->selected = -1;
    devmgr_clamp_scroll(st);
    if (st->truncated) {
        snprintf(st->status, sizeof(st->status),
                 "%u dispositivos encontrados. Lista truncada.", st->count);
    } else {
        snprintf(st->status, sizeof(st->status),
                 "%u dispositivos: %u PCI, %u de bloque.",
                 st->count, pci_devices, block_devices);
    }
    if (st->window) st->window->dirty = true;
}

static gui_rect_t devmgr_client_rect(const devmgr_state_t *st) {
    return st && st->window
        ? bk_gui_window_content_rect_raw(st->window)
        : (gui_rect_t){0, 0, 0, 0};
}

static gui_rect_t devmgr_list_rect(const devmgr_state_t *st) {
    gui_rect_t c = devmgr_client_rect(st);
    return (gui_rect_t){c.x + 16, c.y + 30, c.w - 32, c.h - 54};
}

static gui_rect_t devmgr_row_rect(const devmgr_state_t *st, int index) {
    gui_rect_t list = devmgr_list_rect(st);
    return (gui_rect_t){list.x + 1, list.y + 2 + index * DEVMGR_ROW_H,
                        list.w - 2, DEVMGR_ROW_H};
}

static uint32_t devmgr_visible_rows(const devmgr_state_t *st) {
    gui_rect_t list = devmgr_list_rect(st);
    int h = list.h - 4;
    if (h <= 0) return 0;
    return (uint32_t)(h / DEVMGR_ROW_H);
}

static void devmgr_clamp_scroll(devmgr_state_t *st) {
    uint32_t visible;
    uint32_t max_scroll;

    if (!st) return;
    visible = devmgr_visible_rows(st);
    max_scroll = devmgr_visible_count(st) > visible
        ? devmgr_visible_count(st) - visible
        : 0;
    if (st->scroll > max_scroll) st->scroll = max_scroll;
}

static void devmgr_ensure_selected_visible(devmgr_state_t *st) {
    uint32_t visible;

    if (!st || st->selected < 0) return;
    visible = devmgr_visible_rows(st);
    if (!visible) return;
    if ((uint32_t)st->selected < st->scroll)
        st->scroll = (uint32_t)st->selected;
    else if ((uint32_t)st->selected >= st->scroll + visible)
        st->scroll = (uint32_t)st->selected - visible + 1U;
    devmgr_clamp_scroll(st);
}

static int devmgr_hit_row(const devmgr_state_t *st, int x, int y) {
    gui_rect_t list = devmgr_list_rect(st);
    uint32_t visible = devmgr_visible_rows(st);
    if (x >= list.x + list.w - GUI_SCROLLBAR_SIZE) return -1;
    for (uint32_t i = 0; st && i < visible; i++) {
        uint32_t item = st->scroll + i;
        if (item >= devmgr_visible_count(st)) break;
        if (bk_gui_rect_contains(devmgr_row_rect(st, (int)i), x, y))
            return (int)item;
    }
    return -1;
}

static int devmgr_item_from_visible_row(const devmgr_state_t *st, int row) {
    devmgr_row_type_t type;
    devmgr_section_t section;
    int item_index;

    if (row < 0) return -1;
    if (!devmgr_visible_row_info(st, (uint32_t)row, &type, &section,
                                 &item_index))
        return -1;
    return type == DEVMGR_ROW_DEVICE ? item_index : -1;
}

static void devmgr_open_detail(devmgr_state_t *st, int index);

static bool devmgr_toggle_section_at_row(devmgr_state_t *st, int row) {
    devmgr_row_type_t type;
    devmgr_section_t section;
    int item_index;

    if (!st || row < 0) return false;
    if (!devmgr_visible_row_info(st, (uint32_t)row, &type, &section,
                                 &item_index))
        return false;
    if (type != DEVMGR_ROW_SECTION) return false;
    st->expanded_mask ^= devmgr_section_bit(section);
    devmgr_clamp_scroll(st);
    if (st->selected >= (int)devmgr_visible_count(st))
        st->selected = (int)devmgr_visible_count(st) - 1;
    if (st->window) st->window->dirty = true;
    return true;
}

static void devmgr_open_selected(devmgr_state_t *st) {
    int item_index;

    if (!st || st->selected < 0) return;
    item_index = devmgr_item_from_visible_row(st, st->selected);
    if (item_index >= 0) {
        devmgr_open_detail(st, item_index);
        return;
    }
    (void)devmgr_toggle_section_at_row(st, st->selected);
}

static uint32_t devmgr_status_color(const char *status) {
    if (!status) return CPL_SHADOW;
    if (bk_runtime_strcmp(status, "OK") == 0) return 0x0000A040;
    if (bk_runtime_strcmp(status, "DETECTADO") == 0) return 0x000060B0;
    if (bk_runtime_strcmp(status, "RO") == 0) return 0x00C08000;
    return 0x00C02020;
}

static void devmgr_draw_fallback_icon(gui_surface_t *s, int x, int y,
                                      devmgr_icon_t icon) {
    uint32_t color = 0x000080C0;

    if (icon == DEVMGR_ICON_BLOCK) color = 0x00808080;
    else if (icon == DEVMGR_ICON_DISPLAY) color = 0x000000A0;
    else if (icon == DEVMGR_ICON_AUDIO) color = 0x00A000A0;
    else if (icon == DEVMGR_ICON_INPUT) color = 0x00C0C0C0;
    else if (icon == DEVMGR_ICON_RTC) color = 0x0000A060;
    else if (icon == DEVMGR_ICON_PCI) color = 0x00008040;

    bk_gui_gfx_fill_rect(s, (gui_rect_t){x, y, DEVMGR_ICON_SIZE,
                                      DEVMGR_ICON_SIZE}, CPL_FACE);
    cpl_draw_bevel(s, (gui_rect_t){x + 1, y + 1,
                                   DEVMGR_ICON_SIZE - 2,
                                   DEVMGR_ICON_SIZE - 2},
                   0x00FFFFFF, false);
    bk_gui_gfx_fill_rect(s, (gui_rect_t){x + 5, y + 5, 10, 8}, color);
    bk_gui_gfx_draw_rect(s, (gui_rect_t){x + 5, y + 5, 10, 8}, CPL_DARK);
    bk_gui_gfx_fill_rect(s, (gui_rect_t){x + 8, y + 15, 5, 2}, CPL_DARK);
}

static void devmgr_draw_icon(gui_surface_t *s, int x, int y,
                             const devmgr_state_t *st, devmgr_icon_t icon) {
    if (st && icon < DEVMGR_ICON_COUNT && st->icons[icon]) {
        bk_app_draw_icon(s, x, y, st->icons[icon],
                                 DEVMGR_ICON_SIZE, DEVMGR_ICON_SIZE);
        return;
    }
    devmgr_draw_fallback_icon(s, x, y, icon);
}

static void devmgr_load_icons(devmgr_state_t *st) {
    static const char *paths[DEVMGR_ICON_COUNT] = {
        "/ICONS/SYSTEM.BMP",
        "/ICONS/SYSTEM.BMP",
        "/ICONS/CONFIG.BMP",
        "/ICONS/HDD.BMP",
        "/ICONS/MONITOR.BMP",
        "/ICONS/SOUND.BMP",
        "/ICONS/MOUSE.BMP",
        "/ICONS/DATETIME.BMP",
        "/ICONS/DEVICES.BMP",
    };

    if (!st) return;
    for (uint32_t i = 0; i < DEVMGR_ICON_COUNT; i++) {
        if (st->icons[i]) continue;
        st->icons[i] = bk_app_load_icon(paths[i],
                                                    DEVMGR_ICON_SIZE,
                                                    DEVMGR_ICON_SIZE);
    }
}

static const char *devmgr_icon_path(devmgr_icon_t icon) {
    static const char *paths[DEVMGR_ICON_COUNT] = {
        "/ICONS/SYSTEM.BMP",
        "/ICONS/SYSTEM.BMP",
        "/ICONS/CONFIG.BMP",
        "/ICONS/HDD.BMP",
        "/ICONS/MONITOR.BMP",
        "/ICONS/SOUND.BMP",
        "/ICONS/MOUSE.BMP",
        "/ICONS/DATETIME.BMP",
        "/ICONS/DEVICES.BMP",
    };
    return icon < DEVMGR_ICON_COUNT ? paths[icon] : "/ICONS/DEVICES.BMP";
}

static void devmgr_free_icons(devmgr_state_t *st) {
    if (!st) return;
    for (uint32_t i = 0; i < DEVMGR_ICON_COUNT; i++) {
        if (!st->icons[i]) continue;
        bk_sys_free(st->icons[i]);
        st->icons[i] = NULL;
    }
}

static void devmgr_draw_plus(gui_surface_t *s, gui_rect_t r, bool expanded) {
    bk_gui_gfx_fill_rect(s, r, 0x00FFFFFF);
    bk_gui_gfx_draw_rect(s, r, CPL_SHADOW);
    bk_gui_gfx_draw_line(s, r.x + 2, r.y + 4, r.x + r.w - 3, r.y + 4, CPL_TEXT);
    if (!expanded)
        bk_gui_gfx_draw_line(s, r.x + 4, r.y + 2, r.x + 4, r.y + r.h - 3,
                          CPL_TEXT);
}

static void devmgr_draw_tree(gui_surface_t *s, gui_rect_t list) {
    bk_gui_gfx_fill_rect(s, list, 0x00FFFFFF);
    bk_gui_gfx_draw_rect(s, list, 0x008090A0);
}

static void devmgr_paint(gui_window_t *window UNUSED,
                         gui_surface_t *s,
                         void *context) {
    devmgr_state_t *st = (devmgr_state_t *)context;
    gui_rect_t c;
    gui_rect_t list;
    gui_scrollbar_t scrollbar;
    gui_rect_t status;

    if (!st || !s) return;
    c = devmgr_client_rect(st);
    list = devmgr_list_rect(st);
    status = (gui_rect_t){c.x + 8, c.y + c.h - 18, c.w - 16, 15};

    bk_gui_gfx_fill_rect(s, (gui_rect_t){c.x + 16, c.y + 11, 7, 7},
                      0x00FFFFFF);
    bk_gui_gfx_draw_rect(s, (gui_rect_t){c.x + 16, c.y + 11, 7, 7}, CPL_DARK);
    bk_gui_gfx_fill_rect(s, (gui_rect_t){c.x + 18, c.y + 13, 3, 3},
                      CPL_BLUE);
    bk_gui_font_draw_string(s, c.x + 30, c.y + 9,
        "Ver dispositivos por tipo",
        CPL_TEXT, 0, false);
    bk_gui_font_draw_string(s, c.x + 210, c.y + 9,
        "Doble click abre Propiedades.",
        CPL_SHADOW, 0, false);
    devmgr_draw_tree(s, list);
    bk_gui_scrollbar_init_vertical(&scrollbar,
        (gui_rect_t){list.x + list.w - GUI_SCROLLBAR_SIZE, list.y + 1,
                     GUI_SCROLLBAR_SIZE, list.h - 2},
        st->scroll, devmgr_visible_rows(st), devmgr_visible_count(st));

    for (uint32_t row_index = 0; row_index < devmgr_visible_rows(st);
         row_index++) {
        uint32_t visible_index = st->scroll + row_index;
        gui_rect_t row = devmgr_row_rect(st, (int)row_index);
        devmgr_row_type_t type;
        devmgr_section_t section;
        int item_index;
        bool active;
        int text_x;

        if (!devmgr_visible_row_info(st, visible_index, &type, &section,
                                     &item_index)) break;
        active = (int)visible_index == st->selected ||
                 (int)visible_index == st->hover;

        if (active)
            bk_gui_gfx_fill_rect(s, row,
                              (int)visible_index == st->selected
                                  ? 0x00D8E8F8
                                  : 0x00EEF3F8);

        if (type == DEVMGR_ROW_ROOT) {
            devmgr_draw_icon(s, row.x + 20, row.y + 1, st,
                             DEVMGR_ICON_COMPUTER);
            bk_gui_font_draw_string_clipped(s, row.x + 46, row.y + 6,
                                         "Equipo", CPL_TEXT,
                                         (gui_rect_t){row.x + 46, row.y,
                                                      row.w - 70, row.h});
        } else if (type == DEVMGR_ROW_SECTION) {
            bool expanded = devmgr_section_expanded(st, section);
            devmgr_draw_plus(s, (gui_rect_t){row.x + 16, row.y + 7, 9, 9},
                             expanded);
            devmgr_draw_icon(s, row.x + 30, row.y + 1, st,
                             (devmgr_icon_t)section);
            bk_gui_font_draw_string_clipped(s, row.x + 56, row.y + 6,
                                         devmgr_section_text(section), CPL_TEXT,
                                         (gui_rect_t){row.x + 56, row.y,
                                                      row.w - 88, row.h});
        } else if (item_index >= 0) {
            devmgr_item_t *item = &st->items[item_index];
            bk_gui_gfx_fill_rect(s, (gui_rect_t){row.x + 58, row.y + 8, 6, 6},
                              devmgr_status_color(item->status));
            devmgr_draw_icon(s, row.x + 70, row.y + 1, st,
                             devmgr_icon_for_kind(item->kind));
            text_x = row.x + 96;
            bk_gui_font_draw_string_clipped(s, text_x, row.y + 6, item->name,
                                         CPL_TEXT,
                                         (gui_rect_t){text_x, row.y,
                                                      row.w - 130, row.h});
        }
    }

    bk_gui_scrollbar_paint_vertical(s, &scrollbar);

    cpl_draw_bevel(s, status, CPL_FACE, true);
    bk_gui_font_draw_string_clipped(s, status.x + 4, status.y + 3,
                                 st->status[0] ? st->status : "Listo.",
                                 CPL_TEXT, status);
}

static void devmgr_detail_line(gui_surface_t *s, int x, int y,
                               const char *label, const char *value,
                               gui_rect_t clip) {
    bk_gui_font_draw_string(s, x, y, label, CPL_TEXT, 0, false);
    bk_gui_font_draw_string_clipped(s, x + 110, y, value ? value : "",
                                 CPL_TEXT, clip);
}

static gui_rect_t devmgr_detail_page_rect(gui_window_t *window) {
    gui_rect_t c = bk_gui_window_content_rect_raw(window);
    return (gui_rect_t){c.x + 10, c.y + 34, c.w - 20, c.h - 44};
}

static const cpl_tab_spec_t g_devmgr_detail_tabs[] = {
    {"General", 82},
    {"Driver", 72},
    {"Recursos", 82},
};

static void devmgr_detail_paint(gui_window_t *window UNUSED,
                                gui_surface_t *s,
                                void *context) {
    devmgr_detail_t *d = (devmgr_detail_t *)context;
    gui_rect_t c;
    gui_rect_t page;
    gui_rect_t clip;
    char line[128];
    int x;
    int y;

    if (!d || !window || !s) return;
    c = bk_gui_window_content_rect_raw(window);
    page = devmgr_detail_page_rect(window);
    cpl_draw_tabs(s, c, g_devmgr_detail_tabs, 3, d->active_tab, page, 12, 10);
    clip = (gui_rect_t){page.x + 12, page.y + 14, page.w - 24, page.h - 24};
    x = page.x + 16;
    y = page.y + 18;

    if (d->active_tab == 0) {
        if (d->icon)
            bk_app_draw_icon(s, x, y, d->icon,
                                     DEVMGR_ICON_SIZE, DEVMGR_ICON_SIZE);
        else
            devmgr_draw_fallback_icon(s, x, y,
                                      devmgr_icon_for_kind(d->item.kind));
        devmgr_detail_line(s, x + 28, y + 1, "Nombre:", d->item.name, clip);
        y += 24;
        devmgr_detail_line(s, x, y, "Tipo:",
                           devmgr_kind_text(d->item.kind), clip);
        y += 18;
        devmgr_detail_line(s, x, y, "Estado:", d->item.status, clip);
        y += 18;
        devmgr_detail_line(s, x, y, "Resumen:", d->item.detail, clip);
        y += 24;
        cpl_draw_group(s, (gui_rect_t){x, y, page.w - 32, 58}, "Estado");
        bk_gui_font_draw_string_clipped(s, x + 12, y + 20,
            bk_runtime_strcmp(d->item.status, "OK") == 0
                ? "Este dispositivo funciona correctamente."
                : (bk_runtime_strcmp(d->item.status, "DETECTADO") == 0
                    ? "Detectado en el bus PCI; el driver puede ser parcial."
                    : "El dispositivo requiere atencion."),
            CPL_TEXT, (gui_rect_t){x + 12, y + 16, page.w - 56, 36});
        return;
    }

    if (d->active_tab == 1) {
        cpl_draw_group(s, (gui_rect_t){x, y, page.w - 32, 112},
                       "Controlador");
        devmgr_detail_line(s, x + 14, y + 24, "Driver:", d->item.driver, clip);
        devmgr_detail_line(s, x + 14, y + 42, "Proveedor:", "BlesKernOS",
                           clip);
        devmgr_detail_line(s, x + 14, y + 60, "Estado:", d->item.status,
                           clip);
        devmgr_detail_line(s, x + 14, y + 78, "Detalle:", d->item.detail,
                           clip);
        return;
    }

    cpl_draw_group(s, (gui_rect_t){x, y, page.w - 32, page.h - 32},
                   "Recursos");
    y += 24;
    if (d->item.kind == DEVMGR_KIND_BLOCK) {
        block_device_t *dev = bk_device_block_at(d->item.index);
        if (dev) {
            snprintf(line, sizeof(line), "%u", dev->sector_count);
            devmgr_detail_line(s, x + 14, y, "Sectores:", line, clip);
            y += 18;
            snprintf(line, sizeof(line), "%u", dev->sector_size);
            devmgr_detail_line(s, x + 14, y, "Tam. sector:", line, clip);
            y += 18;
            snprintf(line, sizeof(line), "%u", dev->base_lba);
            devmgr_detail_line(s, x + 14, y, "Base LBA:", line, clip);
            y += 18;
            devmgr_detail_line(s, x + 14, y, "Modo:",
                               dev->read_only ? "Solo lectura" : "Lectura/escritura",
                               clip);
        }
    } else if (d->item.kind == DEVMGR_KIND_PCI) {
        const pci_device_t *dev = bk_device_pci_at(d->item.index);
        if (dev) {
            snprintf(line, sizeof(line), "%04x:%04x",
                     dev->vendor_id, dev->device_id);
            devmgr_detail_line(s, x + 14, y, "ID PCI:", line, clip);
            y += 18;
            snprintf(line, sizeof(line), "%u / %u / %u",
                     dev->bus, dev->slot, dev->function);
            devmgr_detail_line(s, x + 14, y, "Bus/slot/fn:", line, clip);
            y += 18;
            snprintf(line, sizeof(line), "%02x:%02x.%02x",
                     dev->class_code, dev->subclass, dev->prog_if);
            devmgr_detail_line(s, x + 14, y, "Clase:", line, clip);
            y += 18;
            snprintf(line, sizeof(line), "%u", dev->interrupt_line);
            devmgr_detail_line(s, x + 14, y, "IRQ:", line, clip);
        }
    } else if (d->item.kind == DEVMGR_KIND_MEMORY) {
        snprintf(line, sizeof(line), "%u KB",
                 (uint32_t)(d->memory.total_bytes / 1024U));
        devmgr_detail_line(s, x + 14, y, "Total:", line, clip);
        y += 18;
        snprintf(line, sizeof(line), "%u KB",
                 (uint32_t)(d->memory.used_bytes / 1024U));
        devmgr_detail_line(s, x + 14, y, "Usada:", line, clip);
        y += 18;
        snprintf(line, sizeof(line), "%u KB",
                 (uint32_t)(d->memory.free_bytes / 1024U));
        devmgr_detail_line(s, x + 14, y, "Libre:", line, clip);
    } else if (d->item.kind == DEVMGR_KIND_DISPLAY) {
        snprintf(line, sizeof(line), "%ux%ux%u",
                 d->gfx.width, d->gfx.height, d->gfx.bpp);
        devmgr_detail_line(s, x + 14, y, "Modo:", line, clip);
    } else if (d->item.kind == DEVMGR_KIND_INPUT &&
               d->item.index == 0) {
        snprintf(line, sizeof(line), "%u", d->mouse.packets);
        devmgr_detail_line(s, x + 14, y, "Paquetes:", line, clip);
        y += 18;
        snprintf(line, sizeof(line), "0x%02x", (uint32_t)d->mouse.buttons);
        devmgr_detail_line(s, x + 14, y, "Botones:", line, clip);
    } else if (d->item.kind == DEVMGR_KIND_RTC && d->rtc_ok) {
        snprintf(line, sizeof(line), "%u-%02u-%02u %02u:%02u:%02u",
                 d->now.date.year, d->now.date.month, d->now.date.day,
                 d->now.time.hour, d->now.time.minute, d->now.time.second);
        devmgr_detail_line(s, x + 14, y, "Fecha/hora:", line, clip);
    } else {
        devmgr_detail_line(s, x + 14, y, "Recursos:", "No hay recursos extra.",
                           clip);
    }
}

static bool devmgr_detail_event(gui_window_t *window,
                                const gui_event_t *event,
                                void *context) {
    devmgr_detail_t *d = (devmgr_detail_t *)context;
    gui_rect_t c;
    int tab;

    if (!d || !window || !event) return false;
    if (event->type != GUI_EVENT_MOUSE_DOWN) return false;
    c = bk_gui_window_content_rect_raw(window);
    tab = cpl_hit_tab(c, g_devmgr_detail_tabs, 3, 12, 10,
                      event->x, event->y);
    if (tab < 0) return false;
    d->active_tab = tab;
    window->dirty = true;
    return true;
}

static void devmgr_open_detail(devmgr_state_t *st, int index) {
    devmgr_detail_t *detail;
    gui_window_t *window;
    char title[64];

    if (!st || index < 0 || index >= (int)st->count) return;
    detail = (devmgr_detail_t *)bk_sys_alloc_zero(sizeof(*detail));
    if (!detail) return;
    detail->item = st->items[index];
    detail->active_tab = 0;
    detail->icon = bk_app_load_icon(
        devmgr_icon_path(devmgr_icon_for_kind(detail->item.kind)),
        DEVMGR_ICON_SIZE, DEVMGR_ICON_SIZE);
    (void)bk_sys_memory_info(&detail->memory);
    (void)bk_gfx_info(&detail->gfx);
    (void)bk_input_mouse(&detail->mouse);
    detail->rtc_ok = bk_time_datetime(&detail->now);

    snprintf(title, sizeof(title), "Propiedades: %s", detail->item.name);
    window = bk_gui_create_window(st->desktop, 128, 76, 430, 286, title);
    if (!window) {
        bk_sys_free(detail);
        return;
    }
    bk_gui_set_window_content(window, devmgr_detail_paint, detail);
    bk_gui_set_window_event_handler(window, devmgr_detail_event, detail);
    window->owner_pid = bk_sys_getpid();
    snprintf(st->status, sizeof(st->status), "Abriendo propiedades de %s.",
             detail->item.name);
    if (st->window) st->window->dirty = true;
    bk_gui_request_paint();
}

enum {
    DEVMGR_CONTEXT_PROPERTIES = 1,
    DEVMGR_CONTEXT_REFRESH,
};

static void devmgr_context_callback(gui_window_t *window UNUSED,
                                    uint32_t item_id, void *context) {
    devmgr_state_t *st = (devmgr_state_t *)context;
    if (!st) return;
    if (item_id == DEVMGR_CONTEXT_PROPERTIES) devmgr_open_selected(st);
    else if (item_id == DEVMGR_CONTEXT_REFRESH) devmgr_refresh(st);
    if (st->window) st->window->dirty = true;
}

static void devmgr_open_context(devmgr_state_t *st, int hit, int x, int y) {
    devmgr_row_type_t type;
    devmgr_section_t section;
    int item_index;
    bool device = false;
    if (!st || !st->window) return;
    st->selected = hit;
    if (hit >= 0 && devmgr_visible_row_info(st, (uint32_t)hit, &type,
                                            &section, &item_index))
        device = type == DEVMGR_ROW_DEVICE && item_index >= 0;
    bk_gui_window_context_clear(st->window);
    (void)bk_gui_window_context_add_item(st->window,
        DEVMGR_CONTEXT_PROPERTIES, "Propiedades", device,
        devmgr_context_callback, st);
    (void)bk_gui_window_context_add_item(st->window,
        DEVMGR_CONTEXT_REFRESH, "Buscar cambios", true,
        devmgr_context_callback, st);
    bk_gui_window_context_open(st->window, x, y);
}

static bool devmgr_event(gui_window_t *window UNUSED,
                         const gui_event_t *event,
                         void *context) {
    devmgr_state_t *st = (devmgr_state_t *)context;
    int hit;
    gui_rect_t list;
    gui_scrollbar_t scrollbar;

    if (!st || !event) return false;
    list = devmgr_list_rect(st);
    bk_gui_scrollbar_init_vertical(&scrollbar,
        (gui_rect_t){list.x + list.w - GUI_SCROLLBAR_SIZE, list.y + 1,
                     GUI_SCROLLBAR_SIZE, list.h - 2},
        st->scroll, devmgr_visible_rows(st), devmgr_visible_count(st));
    hit = devmgr_hit_row(st, event->x, event->y);

    {
        uint32_t new_scroll;
        if ((st->scrollbar_drag.active || bk_gui_rect_contains(list, event->x, event->y)) &&
            bk_gui_scrollbar_handle_event_vertical(&scrollbar,
                &st->scrollbar_drag, event, 3, &new_scroll)) {
            st->scroll = new_scroll;
            if (st->window) st->window->dirty = true;
            return true;
        }
    }

    if (event->type == GUI_EVENT_MOUSE_MOVE) {
        if (st->hover != hit) {
            st->hover = hit;
            if (st->window) st->window->dirty = true;
        }
        return hit >= 0;
    }

    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        if (event->button == MOUSE_RIGHT_BUTTON) {
            devmgr_open_context(st, hit, event->x, event->y);
            if (st->window) st->window->dirty = true;
            return true;
        }
        st->pressed = hit;
        st->selected = hit;
        if (hit >= 0) {
            devmgr_row_type_t row_type;
            devmgr_section_t row_section;
            int row_item;
            uint32_t visible_row = (uint32_t)(hit - (int)st->scroll);
            gui_rect_t row_rect = devmgr_row_rect(st, (int)visible_row);

            if (devmgr_visible_row_info(st, (uint32_t)hit, &row_type,
                                        &row_section, &row_item) &&
                row_type == DEVMGR_ROW_SECTION &&
                event->x >= row_rect.x + 12 && event->x <= row_rect.x + 30) {
                (void)devmgr_toggle_section_at_row(st, hit);
                return true;
            }
            uint32_t now = bk_sys_ticks();
            if (st->last_clicked == hit &&
                st->last_click_tick &&
                now - st->last_click_tick <= DEVMGR_DBLCLICK_TICKS) {
                st->last_click_tick = 0;
                devmgr_open_selected(st);
            } else {
                st->last_clicked = hit;
                st->last_click_tick = now;
            }
        }
        devmgr_ensure_selected_visible(st);
        if (st->window) st->window->dirty = true;
        return hit >= 0;
    }

    if (event->type == GUI_EVENT_KEY) {
        uint8_t key = (uint8_t)event->key;
        if (key == KEY_UP && devmgr_visible_count(st)) {
            if (st->selected > 0) st->selected--;
            else st->selected = 0;
            devmgr_ensure_selected_visible(st);
            if (st->window) st->window->dirty = true;
            return true;
        }
        if (key == KEY_DOWN && devmgr_visible_count(st)) {
            if (st->selected < 0) st->selected = 0;
            else if ((uint32_t)st->selected + 1U < devmgr_visible_count(st))
                st->selected++;
            devmgr_ensure_selected_visible(st);
            if (st->window) st->window->dirty = true;
            return true;
        }
        if (key == KEY_PGUP) {
            uint32_t visible = devmgr_visible_rows(st);
            st->scroll = st->scroll > visible ? st->scroll - visible : 0;
            if (st->window) st->window->dirty = true;
            return true;
        }
        if (key == KEY_PGDN) {
            st->scroll += devmgr_visible_rows(st);
            devmgr_clamp_scroll(st);
            if (st->window) st->window->dirty = true;
            return true;
        }
        if (key == KEY_ENTER && st->selected >= 0) {
            devmgr_open_selected(st);
            return true;
        }
        if (key == 'r' || key == 'R') {
            devmgr_refresh(st);
            bk_gui_request_paint();
            return true;
        }
    }

    return false;
}

void bleskernos_program_main(gui_desktop_t *desktop) {
    devmgr_state_t *st;

    if (!desktop) return;
    st = (devmgr_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    st->selected = st->hover = st->pressed = st->last_clicked = -1;
    st->expanded_mask = 0xFFFFFFFFU;

    st->window = bk_gui_create_window(desktop, 70, 44,
                                           DEVMGR_WINDOW_W, DEVMGR_WINDOW_H,
                                           "Administrador de dispositivos");
    if (!st->window) {
        bk_sys_free(st);
        return;
    }
    (void)bk_about_attach(st->window, desktop, &(bk_about_info_t){
        "Administrador de dispositivos", "Version 1.0",
        "Hardware, buses y controladores instalados.", "Bles.INC (C) 2026",
        "/ICONS/DEVICES.BMP"});
    bk_gui_set_window_min_size(st->window, DEVMGR_WINDOW_W, DEVMGR_WINDOW_H);
    bk_gui_set_window_content(st->window, devmgr_paint, st);
    bk_gui_set_window_event_handler(st->window, devmgr_event, st);
    st->window->owner_pid = bk_sys_getpid();
    bk_proc_bind_window(st->window);
    devmgr_load_icons(st);

    devmgr_refresh(st);
    st->next_refresh = bk_sys_uptime_ms() + 1000U;
    bk_gui_request_paint();

    while (!bk_proc_exit_requested() && st->window->listed) {
        uint32_t now = bk_sys_uptime_ms();
        if ((int32_t)(now - st->next_refresh) >= 0) {
            devmgr_refresh(st);
            st->next_refresh = now + 1000U;
            bk_gui_request_paint();
        }
        bk_sys_sleep_ticks(4);
    }

    cpl_destroy_window(st->desktop, st->window);
    devmgr_free_icons(st);
    bk_sys_free(st);
    bk_proc_exit();
}
