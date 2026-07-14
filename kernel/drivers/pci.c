#include "../include/pci.h"
#include "../include/memory.h"
#include "../include/pic.h"
#include "../include/vga.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PCI_INVALID_VENDOR 0xFFFF
#define PCI_HEADER_TYPE_MASK 0x7F

static pci_device_t g_pci_devices[PCI_MAX_DEVICES];
static uint32_t g_pci_count = 0;

static uint32_t pci_make_address(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    return 0x80000000U |
           ((uint32_t)bus << 16) |
           ((uint32_t)slot << 11) |
           ((uint32_t)function << 8) |
           (uint32_t)(offset & 0xFC);
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, function, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, slot, function, offset);
    return (uint16_t)((value >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, slot, function, offset);
    return (uint8_t)((value >> ((offset & 3) * 8)) & 0xFF);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, function, offset));
    outl(PCI_CONFIG_DATA, value);
}

void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value) {
    uint16_t data_port = (uint16_t)(PCI_CONFIG_DATA + (offset & 2U));
    outl(PCI_CONFIG_ADDRESS,
         pci_make_address(bus, slot, function, offset));
    /* A direct word write avoids replaying adjacent W1C status bits. */
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(data_port));
}

void pci_config_write8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint8_t value) {
    outl(PCI_CONFIG_ADDRESS,
         pci_make_address(bus, slot, function, offset));
    outb((uint16_t)(PCI_CONFIG_DATA + (offset & 3U)), value);
}

static void pci_add_device(uint8_t bus, uint8_t slot, uint8_t function) {
    if (g_pci_count >= PCI_MAX_DEVICES) return;

    uint16_t vendor = pci_config_read16(bus, slot, function, 0x00);
    if (vendor == PCI_INVALID_VENDOR) return;

    pci_device_t *dev = &g_pci_devices[g_pci_count++];
    kmemset(dev, 0, sizeof(*dev));

    dev->bus = bus;
    dev->slot = slot;
    dev->function = function;
    dev->vendor_id = vendor;
    dev->device_id = pci_config_read16(bus, slot, function, 0x02);
    dev->command = pci_config_read16(bus, slot, function, 0x04);
    dev->status = pci_config_read16(bus, slot, function, 0x06);
    dev->revision_id = pci_config_read8(bus, slot, function, 0x08);
    dev->prog_if = pci_config_read8(bus, slot, function, 0x09);
    dev->subclass = pci_config_read8(bus, slot, function, 0x0A);
    dev->class_code = pci_config_read8(bus, slot, function, 0x0B);
    dev->header_type = pci_config_read8(bus, slot, function, 0x0E);
    dev->interrupt_line = pci_config_read8(bus, slot, function, 0x3C);
    dev->interrupt_pin = pci_config_read8(bus, slot, function, 0x3D);

    if ((dev->header_type & PCI_HEADER_TYPE_MASK) == 0x00) {
        for (uint8_t i = 0; i < PCI_BAR_COUNT; i++) {
            dev->bars[i] = pci_config_read32(bus, slot, function, (uint8_t)(0x10 + i * 4));
        }
    } else if ((dev->header_type & PCI_HEADER_TYPE_MASK) == 0x01) {
        dev->bars[0] = pci_config_read32(bus, slot, function, 0x10);
        dev->bars[1] = pci_config_read32(bus, slot, function, 0x14);
    }
}

static void pci_scan_function(uint8_t bus, uint8_t slot, uint8_t function) {
    if (pci_config_read16(bus, slot, function, 0x00) == PCI_INVALID_VENDOR) return;
    pci_add_device(bus, slot, function);
}

static void pci_scan_slot(uint8_t bus, uint8_t slot) {
    uint16_t vendor = pci_config_read16(bus, slot, 0, 0x00);
    if (vendor == PCI_INVALID_VENDOR) return;

    pci_scan_function(bus, slot, 0);

    uint8_t header_type = pci_config_read8(bus, slot, 0, 0x0E);
    if (header_type & 0x80) {
        for (uint8_t function = 1; function < 8; function++) {
            pci_scan_function(bus, slot, function);
        }
    }
}

void pci_init(void) {
    kmemset(g_pci_devices, 0, sizeof(g_pci_devices));
    g_pci_count = 0;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            pci_scan_slot((uint8_t)bus, slot);
        }
    }

    kprintf("  [PCI] %u dispositivos detectados\n", g_pci_count);
}

uint32_t pci_device_count(void) {
    return g_pci_count;
}

const pci_device_t *pci_device_at(uint32_t index) {
    if (index >= g_pci_count) return NULL;
    return &g_pci_devices[index];
}

bool pci_refresh_device(uint32_t index) {
    pci_device_t *dev;
    uint8_t bar_limit;
    if (index >= g_pci_count) return false;
    dev = &g_pci_devices[index];
    if (pci_config_read16(dev->bus, dev->slot, dev->function, 0x00) == PCI_INVALID_VENDOR) return false;

    dev->command = pci_config_read16(dev->bus, dev->slot, dev->function, 0x04);
    dev->status = pci_config_read16(dev->bus, dev->slot, dev->function, 0x06);
    dev->interrupt_line = pci_config_read8(dev->bus, dev->slot, dev->function, 0x3C);
    dev->interrupt_pin = pci_config_read8(dev->bus, dev->slot, dev->function, 0x3D);

    bar_limit = ((dev->header_type & PCI_HEADER_TYPE_MASK) == 0x01) ? 2 : PCI_BAR_COUNT;
    for (uint8_t i = 0; i < PCI_BAR_COUNT; i++) {
        dev->bars[i] = (i < bar_limit) ? pci_config_read32(dev->bus, dev->slot, dev->function, (uint8_t)(0x10 + i * 4)) : 0;
    }
    return true;
}

int pci_find_by_class(uint8_t class_code, int subclass, uint32_t start_index) {
    for (uint32_t i = start_index; i < g_pci_count; i++) {
        if (g_pci_devices[i].class_code != class_code) continue;
        if (subclass >= 0 && g_pci_devices[i].subclass != (uint8_t)subclass) continue;
        return (int)i;
    }
    return -1;
}

static uint32_t pci_bar_size_from_mask(uint32_t mask, bool is_io) {
    uint32_t masked = is_io ? (mask & 0xFFFFFFFCU) : (mask & 0xFFFFFFF0U);
    if (masked == 0) return 0;
    return (~masked) + 1;
}

bool pci_get_bar_info(const pci_device_t *dev, uint8_t bar_index, pci_bar_info_t *info) {
    uint8_t offset;
    uint32_t raw;
    uint32_t mask;
    bool is_io;

    if (!dev || !info || bar_index >= PCI_BAR_COUNT) return false;
    if ((dev->header_type & PCI_HEADER_TYPE_MASK) == 0x01 && bar_index >= 2) return false;

    kmemset(info, 0, sizeof(*info));
    offset = (uint8_t)(0x10 + bar_index * 4);
    raw = pci_config_read32(dev->bus, dev->slot, dev->function, offset);
    if (raw == 0) return false;

    is_io = (raw & 1) != 0;
    pci_config_write32(dev->bus, dev->slot, dev->function, offset, 0xFFFFFFFFU);
    mask = pci_config_read32(dev->bus, dev->slot, dev->function, offset);
    pci_config_write32(dev->bus, dev->slot, dev->function, offset, raw);

    info->raw = raw;
    info->is_io = is_io;
    if (is_io) {
        info->base = raw & 0xFFFFFFFCU;
        info->size = pci_bar_size_from_mask(mask, true);
    } else {
        info->base = raw & 0xFFFFFFF0U;
        info->prefetchable = (raw & 0x08) != 0;
        info->is_64 = ((raw >> 1) & 0x03) == 0x02;
        info->size = pci_bar_size_from_mask(mask, false);
    }
    return true;
}

bool pci_enable_command(const pci_device_t *dev, uint16_t bits) {
    uint16_t command;
    if (!dev) return false;
    command = pci_config_read16(dev->bus, dev->slot, dev->function, 0x04);
    pci_config_write16(dev->bus, dev->slot, dev->function, 0x04, command | bits);
    return true;
}

const char *pci_class_name(uint8_t class_code, uint8_t subclass) {
    switch (class_code) {
        case 0x00: return "Legacy/Unclassified";
        case 0x01:
            switch (subclass) {
                case 0x00: return "SCSI storage";
                case 0x01: return "IDE storage";
                case 0x02: return "Floppy storage";
                case 0x03: return "IPI storage";
                case 0x04: return "RAID storage";
                case 0x05: return "ATA storage";
                case 0x06: return "SATA storage";
                case 0x07: return "SAS storage";
                case 0x08: return "NVM storage";
                default: return "Mass storage";
            }
        case 0x02: return "Network";
        case 0x03:
            if (subclass == 0x00) return "VGA display";
            if (subclass == 0x02) return "3D display";
            return "Display";
        case 0x04: return "Multimedia";
        case 0x05: return "Memory controller";
        case 0x06:
            switch (subclass) {
                case 0x00: return "Host bridge";
                case 0x01: return "ISA bridge";
                case 0x04: return "PCI bridge";
                default: return "Bridge";
            }
        case 0x07: return "Communication";
        case 0x08: return "System peripheral";
        case 0x09: return "Input controller";
        case 0x0A: return "Docking station";
        case 0x0B: return "Processor";
        case 0x0C:
            switch (subclass) {
                case 0x00: return "FireWire";
                case 0x01: return "ACCESS.bus";
                case 0x02: return "SSA";
                case 0x03: return "USB controller";
                case 0x05: return "SMBus";
                default: return "Serial bus";
            }
        case 0x0D: return "Wireless";
        case 0x0E: return "Intelligent I/O";
        case 0x0F: return "Satellite";
        case 0x10: return "Encryption";
        case 0x11: return "Signal processing";
        default: return "Unknown";
    }
}
