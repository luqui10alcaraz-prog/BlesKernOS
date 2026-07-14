#include "../include/usb_storage.h"
#include "../include/usb_uhci.h"
#include "../include/types.h"
#include "../include/memory.h"
#include "../include/pci.h"
#include "../include/block.h"
#include "../include/vga.h"
#include "../include/task.h"

#define EHCI_MAX_CONTROLLERS 2
#define EHCI_MAX_TD 64
#define EHCI_QH_ALIGN 128U
#define EHCI_TD_ALIGN 32U

#define USB_CLASS_MASS_STORAGE 0x08
#define USB_SUBCLASS_SCSI      0x06
#define USB_PROTOCOL_BULK_ONLY 0x50

#define USB_DIR_IN 0x80
#define USB_EP_ATTR_BULK 0x02

#define REQ_GET_DESC  0x06
#define REQ_SET_ADDR  0x05
#define REQ_SET_CONF  0x09
#define REQ_CLEAR_FEATURE 0x01

#define DESC_DEVICE 1
#define DESC_CONFIG 2
#define DESC_INTERFACE 4
#define DESC_ENDPOINT 5

#define RT_DEV_TO_HOST 0x80
#define RT_HOST_TO_DEV 0x00
#define RT_STANDARD    0x00
#define RT_DEVICE      0x00
#define RT_CLASS       0x20
#define RT_INTERFACE   0x01
#define RT_ENDPOINT    0x02

#define USB_FEATURE_ENDPOINT_HALT 0
#define MSC_REQ_RESET 0xFF

#define EHCI_USB_CMD          0x00
#define EHCI_USB_STS          0x04
#define EHCI_USB_INTR         0x08
#define EHCI_CTRLDSSEGMENT    0x10
#define EHCI_ASYNCLISTADDR    0x18
#define EHCI_CONFIGFLAG       0x40
#define EHCI_PORTSC_BASE      0x44

#define HCCPARAMS_EECP_MASK   0x0000FF00U
#define HCCPARAMS_EECP_SHIFT  8
#define USBLEGSUP_CAPID       0x01
#define USBLEGSUP_HC_BIOS     0x00010000U
#define USBLEGSUP_HC_OS       0x01000000U
#define USBLEGSUP_NEXT_MASK   0x0000FF00U
#define USBLEGSUP_NEXT_SHIFT  8

#define CMD_RS       (1U << 0)
#define CMD_HCRESET  (1U << 1)
#define CMD_ASE      (1U << 5)
#define STS_HCHALTED (1U << 12)

#define PORT_CONNECT (1U << 0)
#define PORT_ENABLE  (1U << 2)
#define PORT_RESET   (1U << 8)
#define PORT_POWER   (1U << 12)
#define PORT_OWNER   (1U << 13)
#define PORT_RWC     ((1U << 1) | (1U << 3) | (1U << 5))

#define PTR_TERM 1U
#define PTR_QH   (1U << 1)

#define TD_ACTIVE      (1U << 7)
#define TD_HALTED      (1U << 6)
#define TD_DATABUFFER  (1U << 5)
#define TD_BABBLE      (1U << 4)
#define TD_XACT        (1U << 3)
#define TD_PID_OUT     0U
#define TD_PID_IN      1U
#define TD_PID_SETUP   2U

#define QH_CH_DTC      (1U << 14)
#define QH_CH_H        (1U << 15)
#define QH_CH_EPS_HIGH (2U << 12)

#define CBW_SIGNATURE 0x43425355U
#define CSW_SIGNATURE 0x53425355U

typedef struct {
    uint8_t len;
    uint8_t type;
    uint16_t usb;
    uint8_t dev_class;
    uint8_t dev_subclass;
    uint8_t dev_protocol;
    uint8_t max_packet0;
    uint16_t vendor;
    uint16_t product;
    uint16_t device;
    uint8_t manufacturer;
    uint8_t product_str;
    uint8_t serial;
    uint8_t configs;
} PACKED usb_device_desc_t;

typedef struct {
    uint8_t len;
    uint8_t type;
    uint16_t total_len;
    uint8_t interfaces;
    uint8_t config_value;
    uint8_t config_str;
    uint8_t attributes;
    uint8_t max_power;
} PACKED usb_config_desc_t;

typedef struct {
    uint8_t len;
    uint8_t type;
    uint8_t number;
    uint8_t alternate;
    uint8_t endpoints;
    uint8_t intf_class;
    uint8_t intf_subclass;
    uint8_t intf_protocol;
    uint8_t intf_str;
} PACKED usb_interface_desc_t;

typedef struct {
    uint8_t len;
    uint8_t type;
    uint8_t addr;
    uint8_t attributes;
    uint16_t max_packet;
    uint8_t interval;
} PACKED usb_endpoint_desc_t;

typedef struct {
    uint8_t type;
    uint8_t req;
    uint16_t value;
    uint16_t index;
    uint16_t len;
} PACKED usb_setup_t;

typedef struct {
    volatile uint32_t link;
    volatile uint32_t alt_link;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
    volatile uint32_t ext_buffer[5];
    uint32_t next;
    uint32_t used;
    uint8_t pad[4];
} PACKED ehci_td_t;

typedef struct {
    volatile uint32_t qhlp;
    volatile uint32_t ch;
    volatile uint32_t caps;
    volatile uint32_t cur_link;
    volatile uint32_t next_link;
    volatile uint32_t alt_link;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
    volatile uint32_t ext_buffer[5];
    uint32_t td_head;
    uint32_t used;
    uint8_t pad[52];
} PACKED ehci_qh_t;

typedef struct {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_len;
    uint8_t flags;
    uint8_t lun;
    uint8_t cb_len;
    uint8_t cb[16];
} PACKED usb_msc_cbw_t;

typedef struct {
    uint32_t signature;
    uint32_t tag;
    uint32_t residue;
    uint8_t status;
} PACKED usb_msc_csw_t;

typedef struct {
    volatile uint8_t *cap;
    volatile uint32_t *op;
    uint32_t cap_len;
    uint32_t port_count;
    ehci_qh_t *async_head;
    ehci_qh_t *qh_pool;
    ehci_td_t *td_pool;
} ehci_controller_t;

typedef struct {
    ehci_controller_t *hc;
    uint8_t addr;
    uint8_t max_packet0;
    uint8_t bulk_in;
    uint8_t bulk_out;
    uint8_t interface_number;
    uint16_t bulk_in_packet;
    uint16_t bulk_out_packet;
    uint8_t toggle_in;
    uint8_t toggle_out;
    uint32_t sectors;
    uint32_t tag;
    char name[8];
} usb_mass_device_t;

static ehci_controller_t g_hc[EHCI_MAX_CONTROLLERS];
static usb_mass_device_t g_mass;
static uint32_t g_hc_count = 0;
static uint32_t g_usb_disk_count = 0;
static uint32_t g_usb_msc_fail_log_budget = 8;

static inline void usb_io_delay(void) {
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}

static void usb_delay_ms(uint32_t ms) {
    for (uint32_t m = 0; m < ms; m++) {
        for (uint32_t i = 0; i < 12000; i++) {
            usb_io_delay();
        }
    }
}

static uint16_t rd16(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

static uint32_t rd32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void wr32be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t mmio_read(ehci_controller_t *hc, uint32_t off) {
    return hc->op[off / 4U];
}

static void mmio_write(ehci_controller_t *hc, uint32_t off, uint32_t value) {
    hc->op[off / 4U] = value;
}

static void *usb_alloc_aligned(uint32_t size, uint32_t align) {
    uintptr_t raw = (uintptr_t)kzalloc(size + align + sizeof(uintptr_t));
    uintptr_t aligned;
    if (!raw) return NULL;
    aligned = (raw + sizeof(uintptr_t) + align - 1U) & ~(uintptr_t)(align - 1U);
    ((uintptr_t *)aligned)[-1] = raw;
    return (void *)aligned;
}

static ehci_td_t *ehci_alloc_td(ehci_controller_t *hc) {
    for (uint32_t i = 0; i < EHCI_MAX_TD; i++) {
        if (!hc->td_pool[i].used) {
            kmemset(&hc->td_pool[i], 0, sizeof(hc->td_pool[i]));
            hc->td_pool[i].used = true;
            return &hc->td_pool[i];
        }
    }
    return NULL;
}

static void ehci_free_chain(ehci_td_t *td) {
    while (td) {
        ehci_td_t *next = (ehci_td_t *)(uintptr_t)td->next;
        td->used = false;
        td = next;
    }
}

static void ehci_td_init(ehci_td_t *td, ehci_td_t *prev, uint32_t pid,
                         uint32_t toggle, const void *data, uint32_t len) {
    uintptr_t p = (uintptr_t)data;
    if (prev) {
        prev->link = (uint32_t)(uintptr_t)td;
        prev->next = (uint32_t)(uintptr_t)td;
    }
    td->link = PTR_TERM;
    td->alt_link = PTR_TERM;
    td->next = 0;
    td->token = TD_ACTIVE | (3U << 10) | (pid << 8) |
                (len << 16) | (toggle << 31);
    for (uint32_t i = 0; i < 5; i++) {
        td->buffer[i] = (uint32_t)((p & ~0xFFFU) + (i * 0x1000U));
        td->ext_buffer[i] = 0;
    }
    td->buffer[0] = (uint32_t)p;
}

static bool ehci_run_qh(ehci_controller_t *hc, ehci_qh_t *qh,
                        ehci_td_t *head, bool log_errors) {
    ehci_qh_t *async = hc->async_head;
    uint32_t timeout = 1000000;
    bool ok;

    qh->qhlp = async->qhlp;
    qh->td_head = (uint32_t)(uintptr_t)head;
    qh->cur_link = 0;
    qh->next_link = (uint32_t)(uintptr_t)head;
    qh->alt_link = PTR_TERM;
    qh->token = 0;
    async->qhlp = (uint32_t)(uintptr_t)qh | PTR_QH;

    while (timeout-- && !(qh->next_link & PTR_TERM)) {
        if (mmio_read(hc, EHCI_USB_STS) & (1U << 4)) {
            if (log_errors) {
                kprintf("  [USB] EHCI host system error sts=%x qh=%x token=%x next=%x\n",
                        mmio_read(hc, EHCI_USB_STS),
                        (uint32_t)(uintptr_t)qh, qh->token, qh->next_link);
            }
            break;
        }
        __asm__ volatile ("pause");
    }

    async->qhlp = (uint32_t)(uintptr_t)async | PTR_QH;
    ok = timeout != 0 &&
         !(qh->token & (TD_ACTIVE | TD_HALTED | TD_DATABUFFER |
                        TD_BABBLE | TD_XACT));
    if (!ok && log_errors) {
        kprintf("  [USB] EHCI transfer fail sts=%x qh=%x token=%x next=%x\n",
                mmio_read(hc, EHCI_USB_STS),
                (uint32_t)(uintptr_t)qh, qh->token, qh->next_link);
    }
    ehci_free_chain(head);
    qh->used = false;
    mmio_write(hc, EHCI_USB_STS, 0x3F);
    return ok;
}

static ehci_qh_t *ehci_alloc_qh(ehci_controller_t *hc, uint8_t addr,
                                uint8_t ep, uint16_t max_packet) {
    for (uint32_t i = 1; i < 8; i++) {
        ehci_qh_t *qh = &hc->qh_pool[i];
        if (!qh->used) {
            kmemset(qh, 0, sizeof(*qh));
            qh->used = true;
            qh->ch = (uint32_t)addr |
                     ((uint32_t)ep << 8) |
                     QH_CH_EPS_HIGH |
                     QH_CH_DTC |
                     ((uint32_t)max_packet << 16) |
                     (5U << 28);
            qh->caps = 1U << 30;
            qh->next_link = PTR_TERM;
            qh->alt_link = PTR_TERM;
            return qh;
        }
    }
    return NULL;
}

static bool ehci_control(ehci_controller_t *hc, uint8_t addr, uint8_t max_packet,
                         usb_setup_t *setup, void *data, uint32_t len) {
    ehci_td_t *head;
    ehci_td_t *td;
    ehci_td_t *prev;
    ehci_qh_t *qh;
    uint32_t pid;
    uint32_t toggle = 1;
    uint8_t *it = (uint8_t *)data;

    head = ehci_alloc_td(hc);
    if (!head) return false;
    prev = head;
    ehci_td_init(head, NULL, TD_PID_SETUP, 0, setup, sizeof(*setup));

    pid = (setup->type & USB_DIR_IN) ? TD_PID_IN : TD_PID_OUT;
    while (len) {
        uint32_t chunk = len > max_packet ? max_packet : len;
        td = ehci_alloc_td(hc);
        if (!td) return false;
        ehci_td_init(td, prev, pid, toggle, it, chunk);
        prev = td;
        toggle ^= 1;
        it += chunk;
        len -= chunk;
    }

    td = ehci_alloc_td(hc);
    if (!td) return false;
    ehci_td_init(td, prev, (setup->type & USB_DIR_IN) ? TD_PID_OUT : TD_PID_IN,
                 1, NULL, 0);

    qh = ehci_alloc_qh(hc, addr, 0, max_packet);
    if (!qh) return false;
    return ehci_run_qh(hc, qh, head, true);
}

static bool usb_request(ehci_controller_t *hc, uint8_t addr, uint8_t max_packet,
                        uint8_t type, uint8_t req, uint16_t value,
                        uint16_t index, uint16_t len, void *data) {
    usb_setup_t setup;
    setup.type = type;
    setup.req = req;
    setup.value = value;
    setup.index = index;
    setup.len = len;
    return ehci_control(hc, addr, max_packet, &setup, data, len);
}

static bool usb_clear_halt(usb_mass_device_t *dev, uint8_t endpoint) {
    return usb_request(dev->hc, dev->addr, dev->max_packet0,
                       RT_HOST_TO_DEV | RT_STANDARD | RT_ENDPOINT,
                       REQ_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_HALT,
                       endpoint, 0, NULL);
}

static bool usb_msc_reset_recovery(usb_mass_device_t *dev) {
    bool ok;
    if (!dev || !dev->hc) return false;

    ok = usb_request(dev->hc, dev->addr, dev->max_packet0,
                     RT_HOST_TO_DEV | RT_CLASS | RT_INTERFACE,
                     MSC_REQ_RESET, 0, dev->interface_number, 0, NULL);
    usb_delay_ms(20);
    ok = usb_clear_halt(dev, dev->bulk_in) && ok;
    ok = usb_clear_halt(dev, dev->bulk_out) && ok;
    dev->toggle_in = 0;
    dev->toggle_out = 0;
    mmio_write(dev->hc, EHCI_USB_STS, 0x3F);
    return ok;
}

static bool ehci_bulk(usb_mass_device_t *dev, bool in, void *data,
                      uint32_t len) {
    ehci_controller_t *hc = dev->hc;
    uint8_t ep = in ? (dev->bulk_in & 0x0F) : (dev->bulk_out & 0x0F);
    uint16_t max_packet = in ? dev->bulk_in_packet : dev->bulk_out_packet;
    uint8_t *it = (uint8_t *)data;
    uint32_t left = len;
    ehci_td_t *head = NULL;
    ehci_td_t *prev = NULL;
    ehci_qh_t *qh;
    uint32_t pid = in ? TD_PID_IN : TD_PID_OUT;
    uint32_t toggle = in ? dev->toggle_in : dev->toggle_out;

    if (!max_packet) max_packet = 512;
    if (len == 0) return true;

    while (left) {
        uint32_t chunk = left > 16384U ? 16384U : left;
        uint32_t packets = (chunk + max_packet - 1U) / max_packet;
        ehci_td_t *td = ehci_alloc_td(hc);
        if (!td) return false;
        if (!head) head = td;
        ehci_td_init(td, prev, pid, toggle, it, chunk);
        prev = td;
        toggle ^= (uint8_t)(packets & 1U);
        it += chunk;
        left -= chunk;
    }

    qh = ehci_alloc_qh(hc, dev->addr, ep, max_packet);
    if (!qh) return false;
    if (!ehci_run_qh(hc, qh, head, false)) return false;
    if (in) dev->toggle_in = (uint8_t)toggle;
    else dev->toggle_out = (uint8_t)toggle;
    return true;
}

static bool usb_msc_command(usb_mass_device_t *dev, const uint8_t *cmd,
                            uint8_t cmd_len, bool in, void *data,
                            uint32_t data_len) {
    usb_msc_cbw_t cbw;
    usb_msc_csw_t csw;
    kmemset(&cbw, 0, sizeof(cbw));
    cbw.signature = CBW_SIGNATURE;
    cbw.tag = ++dev->tag;
    cbw.data_len = data_len;
    cbw.flags = in ? USB_DIR_IN : 0;
    cbw.cb_len = cmd_len;
    kmemcpy(cbw.cb, cmd, cmd_len);

    if (!ehci_bulk(dev, false, &cbw, sizeof(cbw))) return false;
    if (data_len && !ehci_bulk(dev, in, data, data_len)) return false;
    kmemset(&csw, 0, sizeof(csw));
    if (!ehci_bulk(dev, true, &csw, sizeof(csw))) return false;
    return csw.signature == CSW_SIGNATURE && csw.tag == cbw.tag &&
           csw.status == 0;
}

static bool usb_msc_command_retry(usb_mass_device_t *dev, const uint8_t *cmd,
                                  uint8_t cmd_len, bool in, void *data,
                                  uint32_t data_len) {
    if (usb_msc_command(dev, cmd, cmd_len, in, data, data_len))
        return true;
    if (!usb_msc_reset_recovery(dev))
        return false;
    if (usb_msc_command(dev, cmd, cmd_len, in, data, data_len))
        return true;
    if (g_usb_msc_fail_log_budget) {
        kprintf("  [USB] MSC command failed after recovery op=0x%x len=%u\n",
                cmd && cmd_len ? cmd[0] : 0, data_len);
        g_usb_msc_fail_log_budget--;
    }
    return false;
}

static bool usb_msc_read_capacity(usb_mass_device_t *dev) {
    uint8_t cmd[10];
    uint8_t data[8];
    kmemset(cmd, 0, sizeof(cmd));
    cmd[0] = 0x25;
    if (!usb_msc_command_retry(dev, cmd, sizeof(cmd), true, data, sizeof(data)))
        return false;
    dev->sectors = rd32be(data) + 1U;
    return rd32be(data + 4) == BLOCK_SECTOR_SIZE && dev->sectors != 0;
}

static bool usb_msc_rw(usb_mass_device_t *dev, uint32_t lba, uint8_t count,
                       void *buffer, bool write) {
    uint8_t cmd[10];
    kmemset(cmd, 0, sizeof(cmd));
    cmd[0] = write ? 0x2A : 0x28;
    wr32be(cmd + 2, lba);
    cmd[7] = 0;
    cmd[8] = count;
    return usb_msc_command_retry(dev, cmd, sizeof(cmd), !write, buffer,
                                 (uint32_t)count * BLOCK_SECTOR_SIZE);
}

static bool usb_block_read(block_device_t *block, uint32_t lba, uint8_t count,
                           void *buffer) {
    usb_mass_device_t *dev = (usb_mass_device_t *)block->driver_data;
    return dev && buffer && count && usb_msc_rw(dev, lba, count, buffer, false);
}

static bool usb_block_write(block_device_t *block, uint32_t lba, uint8_t count,
                            const void *buffer) {
    usb_mass_device_t *dev = (usb_mass_device_t *)block->driver_data;
    return dev && buffer && count &&
           usb_msc_rw(dev, lba, count, (void *)buffer, true);
}

static bool usb_try_mass_storage(ehci_controller_t *hc, uint32_t port) {
    usb_device_desc_t dd;
    uint8_t cfg[256];
    usb_config_desc_t *cd = (usb_config_desc_t *)cfg;
    usb_interface_desc_t *picked_intf = NULL;
    uint8_t addr = 1;
    uint8_t config_value;
    uint8_t bulk_in = 0, bulk_out = 0;
    uint16_t bulk_in_packet = 0, bulk_out_packet = 0;
    uint8_t *p;
    uint8_t *end;

    (void)port;
    kmemset(&dd, 0, sizeof(dd));
    if (!usb_request(hc, 0, 8, RT_DEV_TO_HOST | RT_STANDARD | RT_DEVICE,
                     REQ_GET_DESC, (DESC_DEVICE << 8), 0, 8, &dd))
        return false;
    if (dd.max_packet0 == 0) dd.max_packet0 = 8;
    if (!usb_request(hc, 0, dd.max_packet0,
                     RT_HOST_TO_DEV | RT_STANDARD | RT_DEVICE,
                     REQ_SET_ADDR, addr, 0, 0, NULL))
        return false;
    usb_delay_ms(2);

    if (!usb_request(hc, addr, dd.max_packet0,
                     RT_DEV_TO_HOST | RT_STANDARD | RT_DEVICE,
                     REQ_GET_DESC, (DESC_DEVICE << 8), 0, sizeof(dd), &dd))
        return false;

    if (!usb_request(hc, addr, dd.max_packet0,
                     RT_DEV_TO_HOST | RT_STANDARD | RT_DEVICE,
                     REQ_GET_DESC, (DESC_CONFIG << 8), 0, 4, cfg))
        return false;
    if (cd->total_len > sizeof(cfg)) return false;
    if (!usb_request(hc, addr, dd.max_packet0,
                     RT_DEV_TO_HOST | RT_STANDARD | RT_DEVICE,
                     REQ_GET_DESC, (DESC_CONFIG << 8), 0, cd->total_len, cfg))
        return false;

    config_value = cd->config_value;
    p = cfg + cd->len;
    end = cfg + cd->total_len;
    while (p + 2 <= end && p[0]) {
        if (p + p[0] > end) break;
        if (p[1] == DESC_INTERFACE) {
            usb_interface_desc_t *id = (usb_interface_desc_t *)p;
            if (id->intf_class == USB_CLASS_MASS_STORAGE &&
                id->intf_subclass == USB_SUBCLASS_SCSI &&
                id->intf_protocol == USB_PROTOCOL_BULK_ONLY) {
                picked_intf = id;
                bulk_in = bulk_out = 0;
            } else if (!picked_intf) {
                bulk_in = bulk_out = 0;
            }
        } else if (picked_intf && p[1] == DESC_ENDPOINT) {
            usb_endpoint_desc_t *ed = (usb_endpoint_desc_t *)p;
            if ((ed->attributes & 0x03) == USB_EP_ATTR_BULK) {
                if (ed->addr & USB_DIR_IN) {
                    bulk_in = ed->addr;
                    bulk_in_packet = rd16(&ed->max_packet);
                } else {
                    bulk_out = ed->addr;
                    bulk_out_packet = rd16(&ed->max_packet);
                }
            }
        }
        p += p[0];
    }

    if (!picked_intf || !bulk_in || !bulk_out) return false;
    if (!usb_request(hc, addr, dd.max_packet0,
                     RT_HOST_TO_DEV | RT_STANDARD | RT_DEVICE,
                     REQ_SET_CONF, config_value, 0, 0, NULL))
        return false;
    usb_delay_ms(20);

    kmemset(&g_mass, 0, sizeof(g_mass));
    g_mass.hc = hc;
    g_mass.addr = addr;
    g_mass.max_packet0 = dd.max_packet0;
    g_mass.bulk_in = bulk_in;
    g_mass.bulk_out = bulk_out;
    g_mass.interface_number = picked_intf->number;
    g_mass.bulk_in_packet = bulk_in_packet;
    g_mass.bulk_out_packet = bulk_out_packet;
    g_mass.name[0] = 'u';
    g_mass.name[1] = 's';
    g_mass.name[2] = 'b';
    g_mass.name[3] = (char)('0' + g_usb_disk_count++);
    g_mass.name[4] = '\0';

    if (!usb_msc_read_capacity(&g_mass)) {
        kprintf("  [USB] Mass storage sin capacidad valida\n");
        return false;
    }

    if (!block_register_ex(g_mass.name, BLOCK_DEVICE_USB, g_mass.sectors,
                           BLOCK_SECTOR_SIZE, false, &g_mass,
                           usb_block_read))
        return false;
    block_set_writer(g_mass.name, usb_block_write);
    kprintf("  [USB] %s: mass storage %u sectores\n",
            g_mass.name, g_mass.sectors);
    return true;
}

static bool ehci_reset_port(ehci_controller_t *hc, uint32_t port) {
    volatile uint32_t *reg = &hc->op[EHCI_PORTSC_BASE / 4U + port];
    uint32_t status = *reg;
    if (!(status & PORT_CONNECT)) return false;
    if (status & PORT_OWNER) {
        kprintf("  [USB] puerto %u delegado a companion, status=%x\n", port, status);
        return false;
    }
    *reg = (status | PORT_POWER) & ~PORT_RWC;
    usb_delay_ms(20);
    status = *reg;
    *reg = (status | PORT_RESET | PORT_POWER) & ~PORT_RWC;
    usb_delay_ms(50);
    status = *reg;
    *reg = (status & ~PORT_RESET) & ~PORT_RWC;
    usb_delay_ms(100);
    status = *reg;
    kprintf("  [USB] puerto %u status=%x\n", port, status);
    return (status & PORT_ENABLE) != 0;
}

static void ehci_take_ownership(const pci_device_t *pci, uint32_t hcc_params) {
    uint8_t eecp = (uint8_t)((hcc_params & HCCPARAMS_EECP_MASK) >>
                             HCCPARAMS_EECP_SHIFT);
    uint32_t guard = 0;

    while (eecp >= 0x40 && guard++ < 16) {
        uint32_t cap = pci_config_read32(pci->bus, pci->slot,
                                         pci->function, eecp);
        if ((cap & 0xFFU) == USBLEGSUP_CAPID) {
            if (cap & USBLEGSUP_HC_BIOS) {
                kprintf("  [USB] EHCI legacy BIOS owned, pidiendo ownership\n");
                pci_config_write32(pci->bus, pci->slot, pci->function,
                                   eecp, cap | USBLEGSUP_HC_OS);
                for (uint32_t i = 0; i < 100000; i++) {
                    cap = pci_config_read32(pci->bus, pci->slot,
                                            pci->function, eecp);
                    if (!(cap & USBLEGSUP_HC_BIOS) &&
                        (cap & USBLEGSUP_HC_OS))
                        break;
                }
            }
            pci_config_write32(pci->bus, pci->slot, pci->function,
                               eecp + 4, 0);
            return;
        }
        eecp = (uint8_t)((cap & USBLEGSUP_NEXT_MASK) >>
                         USBLEGSUP_NEXT_SHIFT);
    }
}

static void ehci_init_controller(const pci_device_t *pci) {
    pci_bar_info_t bar;
    ehci_controller_t *hc;
    uint32_t hcs;
    uint32_t hcc;
    uint32_t timeout;
    uint32_t cmd;

    if (g_hc_count >= EHCI_MAX_CONTROLLERS) return;
    if (!pci_get_bar_info(pci, 0, &bar) || bar.is_io || !bar.base) return;
    if (sizeof(ehci_qh_t) != 128U || sizeof(ehci_td_t) != 64U) {
        kprintf("  [USB] EHCI struct size invalido qh=%u td=%u\n",
                sizeof(ehci_qh_t), sizeof(ehci_td_t));
        return;
    }

    pci_enable_command(pci, PCI_COMMAND_MEMORY | PCI_COMMAND_BUSMASTER);

    hc = &g_hc[g_hc_count++];
    kmemset(hc, 0, sizeof(*hc));
    hc->cap = (volatile uint8_t *)(uintptr_t)bar.base;
    hc->cap_len = hc->cap[0];
    hc->op = (volatile uint32_t *)(uintptr_t)(bar.base + hc->cap_len);
    hcs = *(volatile uint32_t *)(uintptr_t)(bar.base + 4);
    hcc = *(volatile uint32_t *)(uintptr_t)(bar.base + 8);
    hc->port_count = hcs & 0x0F;

    ehci_take_ownership(pci, hcc);

    cmd = mmio_read(hc, EHCI_USB_CMD);
    mmio_write(hc, EHCI_USB_CMD, cmd & ~CMD_RS);
    timeout = 100000;
    while (timeout-- && !(mmio_read(hc, EHCI_USB_STS) & STS_HCHALTED)) {}
    mmio_write(hc, EHCI_USB_CMD, CMD_HCRESET);
    timeout = 100000;
    while (timeout-- && (mmio_read(hc, EHCI_USB_CMD) & CMD_HCRESET)) {}

    hc->qh_pool = (ehci_qh_t *)usb_alloc_aligned(sizeof(ehci_qh_t) * 8,
                                                 EHCI_QH_ALIGN);
    hc->td_pool = (ehci_td_t *)usb_alloc_aligned(sizeof(ehci_td_t) * EHCI_MAX_TD,
                                                 EHCI_TD_ALIGN);
    if (!hc->qh_pool || !hc->td_pool) return;

    hc->async_head = &hc->qh_pool[0];
    kmemset(hc->async_head, 0, sizeof(*hc->async_head));
    hc->async_head->used = true;
    hc->async_head->qhlp = (uint32_t)(uintptr_t)hc->async_head | PTR_QH;
    hc->async_head->ch = QH_CH_H | QH_CH_EPS_HIGH | QH_CH_DTC |
                         (64U << 16);
    hc->async_head->caps = 1U << 30;
    hc->async_head->next_link = PTR_TERM;
    hc->async_head->alt_link = PTR_TERM;

    mmio_write(hc, EHCI_CTRLDSSEGMENT, 0);
    mmio_write(hc, EHCI_USB_INTR, 0);
    mmio_write(hc, EHCI_ASYNCLISTADDR,
               (uint32_t)(uintptr_t)hc->async_head);
    mmio_write(hc, EHCI_CONFIGFLAG, 1);
    mmio_write(hc, EHCI_USB_STS, 0x3F);
    mmio_write(hc, EHCI_USB_CMD, CMD_RS | CMD_ASE);
    usb_delay_ms(50);

    kprintf("  [USB] EHCI %u puertos en MMIO %x\n", hc->port_count, bar.base);
    for (uint32_t port = 0; port < hc->port_count; port++) {
        if (ehci_reset_port(hc, port) && usb_try_mass_storage(hc, port))
            return;
    }
}

static void usb_storage_scan_ports(void) {
    if (g_usb_disk_count != 0) return;
    for (uint32_t h = 0; h < g_hc_count; h++) {
        ehci_controller_t *hc = &g_hc[h];
        if (!hc->op) continue;
        for (uint32_t port = 0; port < hc->port_count; port++) {
            if (ehci_reset_port(hc, port) && usb_try_mass_storage(hc, port))
                return;
        }
    }
}

static void usb_storage_hotplug_main(void *arg UNUSED) {
    for (;;) {
        usb_storage_scan_ports();
        task_sleep(100);
    }
}

void usb_storage_init(void) {
    int index = -1;
    g_hc_count = 0;
    g_usb_disk_count = 0;
    for (;;) {
        index = pci_find_by_class(0x0C, 0x03, (uint32_t)(index + 1));
        if (index < 0) break;
        const pci_device_t *dev = pci_device_at((uint32_t)index);
        if (dev && dev->prog_if == 0x20)
            ehci_init_controller(dev);
    }

    /* USB 1.1 companion/legacy controllers, including Intel PIIX4M. */
    usb_uhci_init();
}

void usb_storage_start_hotplug_task(void) {
    if (g_hc_count != 0)
        (void)task_create("usb-hotplug", usb_storage_hotplug_main, NULL);
    usb_uhci_start_hotplug_task();
}
