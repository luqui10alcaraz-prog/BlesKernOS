#ifndef PCI_H
#define PCI_H

#include "types.h"

#define PCI_MAX_DEVICES 128
#define PCI_BAR_COUNT 6

#define PCI_COMMAND_IO        0x0001
#define PCI_COMMAND_MEMORY    0x0002
#define PCI_COMMAND_BUSMASTER 0x0004

typedef struct {
    uint32_t raw;
    uint32_t base;
    uint32_t size;
    bool is_io;
    bool is_64;
    bool prefetchable;
} pci_bar_info_t;

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    uint8_t revision_id;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t class_code;
    uint8_t header_type;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    uint32_t bars[PCI_BAR_COUNT];
} pci_device_t;

void pci_init(void);
uint32_t pci_device_count(void);
const pci_device_t *pci_device_at(uint32_t index);
const char *pci_class_name(uint8_t class_code, uint8_t subclass);
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value);
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value);
void pci_config_write8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint8_t value);
int pci_find_by_class(uint8_t class_code, int subclass, uint32_t start_index);
bool pci_get_bar_info(const pci_device_t *dev, uint8_t bar_index, pci_bar_info_t *info);
bool pci_enable_command(const pci_device_t *dev, uint16_t bits);
bool pci_refresh_device(uint32_t index);

#endif
