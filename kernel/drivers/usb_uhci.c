#include "../include/usb_uhci.h"
#include "../include/types.h"
#include "../include/memory.h"
#include "../include/pci.h"
#include "../include/block.h"
#include "../include/vga.h"
#include "../include/task.h"
#include "../include/pic.h"
#include "../include/pit.h"

/*
 * Minimal UHCI host-controller driver for USB 1.1 mass-storage devices.
 *
 * The Dell Latitude C600 generation uses the Intel PIIX4/PIIX4M USB host
 * controller, which exposes UHCI (PCI class 0C/03, programming interface 00).
 * The BIOS can boot from that controller, but BlesKernOS must take ownership,
 * rebuild a UHCI frame schedule and enumerate the flash drive again after the
 * protected-mode kernel starts.
 *
 * This driver intentionally mirrors the existing EHCI mass-storage path while
 * keeping the two HCDs independent.  It supports one BOT/SCSI disk per UHCI
 * controller and registers it through the normal block layer as usb0, usb1,
 * and so on.
 */

#define UHCI_MAX_CONTROLLERS 4
#define UHCI_MAX_TD          128
#define UHCI_FRAME_COUNT     1024
#define UHCI_FRAME_ALIGN     4096U
#define UHCI_QH_ALIGN        16U
#define UHCI_TD_ALIGN        16U
#define UHCI_ROOT_PORTS      2
#define UHCI_CTRL_DMA_SIZE   1024U
#define UHCI_DIAG_MAX_TDS    12U

/*
 * Timeouts are measured with UHCI FRNUM, which advances once per USB frame
 * even during early boot while PIT interrupts are still disabled.
 */
#define UHCI_CONTROL_TIMEOUT_MS 2000U
#define UHCI_BULK_TIMEOUT_MS   10000U
#define UHCI_CBW_TIMEOUT_MS      3000U
#define UHCI_CSW_TIMEOUT_MS      3000U
#define UHCI_FRAME_STALL_GUARD 50000000U
#define USB_CONNECT_DEBOUNCE_MS  100U
#define USB_RESET_ASSERT_MS       55U
#define USB_RESET_RECOVERY_MS      2U
#define MSC_POST_CBW_DELAY_MS       1U
#define MSC_RESET_PRE_CLEAR_MS     50U
#define MSC_RESET_POST_CLEAR_MS   250U

#define USB_CLASS_MASS_STORAGE 0x08
#define USB_SUBCLASS_SCSI      0x06
#define USB_PROTOCOL_BULK_ONLY 0x50

#define USB_DIR_IN       0x80
#define USB_EP_ATTR_BULK 0x02

#define REQ_GET_DESC      0x06
#define REQ_SET_ADDR      0x05
#define REQ_GET_CONF      0x08
#define REQ_SET_CONF      0x09
#define REQ_SET_INTERFACE 0x0B
#define REQ_CLEAR_FEATURE 0x01

#define DESC_DEVICE    1
#define DESC_CONFIG    2
#define DESC_INTERFACE 4
#define DESC_ENDPOINT  5

#define RT_DEV_TO_HOST 0x80
#define RT_HOST_TO_DEV 0x00
#define RT_STANDARD    0x00
#define RT_DEVICE      0x00
#define RT_CLASS       0x20
#define RT_INTERFACE   0x01
#define RT_ENDPOINT    0x02

#define USB_FEATURE_ENDPOINT_HALT 0
#define MSC_REQ_GET_MAX_LUN       0xFE
#define MSC_REQ_RESET             0xFF

#define UHCI_USBCMD    0x00
#define UHCI_USBSTS    0x02
#define UHCI_USBINTR   0x04
#define UHCI_FRNUM     0x06
#define UHCI_FLBASEADD 0x08
#define UHCI_SOFMOD    0x0C
#define UHCI_PORTSC0   0x10

#define UHCI_CMD_RS       (1U << 0)
#define UHCI_CMD_HCRESET  (1U << 1)
#define UHCI_CMD_GRESET   (1U << 2)
#define UHCI_CMD_SWDBG    (1U << 5)
#define UHCI_CMD_CF       (1U << 6)
#define UHCI_CMD_MAXP     (1U << 7)
#define UHCI_CMD_RUN      (UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP)

#define UHCI_STS_USBINT   (1U << 0)
#define UHCI_STS_USBERR   (1U << 1)
#define UHCI_STS_RESUME   (1U << 2)
#define UHCI_STS_HSE      (1U << 3)
#define UHCI_STS_HCPE     (1U << 4)
#define UHCI_STS_HALTED   (1U << 5)
#define UHCI_STS_CLEAR    (UHCI_STS_USBINT | UHCI_STS_USBERR | \
                           UHCI_STS_RESUME | UHCI_STS_HSE | \
                           UHCI_STS_HCPE)

#define UHCI_PORT_CONNECT       (1U << 0)
#define UHCI_PORT_CONNECT_CHG   (1U << 1)
#define UHCI_PORT_ENABLE        (1U << 2)
#define UHCI_PORT_ENABLE_CHG    (1U << 3)
#define UHCI_PORT_LINE_MASK     (3U << 4)
#define UHCI_PORT_RESUME        (1U << 6)
#define UHCI_PORT_ALWAYS1       (1U << 7)
#define UHCI_PORT_LOW_SPEED     (1U << 8)
#define UHCI_PORT_RESET         (1U << 9)
#define UHCI_PORT_OVERCURRENT   (1U << 10)
#define UHCI_PORT_OC_CHG        (1U << 11)
#define UHCI_PORT_SUSPEND       (1U << 12)
#define UHCI_PORT_RW_MASK       (UHCI_PORT_ENABLE | \
                                 UHCI_PORT_RESUME | \
                                 UHCI_PORT_ALWAYS1 | \
                                 UHCI_PORT_RESET | \
                                 UHCI_PORT_SUSPEND)
#define UHCI_PORT_RWC           (UHCI_PORT_CONNECT_CHG | \
                                 UHCI_PORT_ENABLE_CHG | \
                                 UHCI_PORT_OC_CHG)

#define UHCI_LINK_TERM  1U
#define UHCI_LINK_QH    (1U << 1)
#define UHCI_LINK_DEPTH (1U << 2)

#define UHCI_TD_STS_BITSTUFF   (1U << 17)
#define UHCI_TD_STS_CRC_TIMEOUT (1U << 18)
#define UHCI_TD_STS_NAK        (1U << 19)
#define UHCI_TD_STS_BABBLE     (1U << 20)
#define UHCI_TD_STS_DATABUFFER (1U << 21)
#define UHCI_TD_STS_STALLED    (1U << 22)
#define UHCI_TD_STS_ACTIVE     (1U << 23)
#define UHCI_TD_STS_IOC        (1U << 24)
#define UHCI_TD_STS_LOW_SPEED  (1U << 26)
#define UHCI_TD_STS_ERRCNT(n)  (((uint32_t)(n) & 3U) << 27)
#define UHCI_TD_STS_ERRCNT_MASK (3U << 27)
#define UHCI_TD_STS_SPD        (1U << 29)
#define UHCI_TD_STS_RESERVED16  (1U << 16)
#define UHCI_TD_ACTLEN_NULL    0x000007FFU
#define UHCI_TD_ACTLEN_MASK    0x000007FFU
#define UHCI_TD_STS_INITIAL_BAD (UHCI_TD_STS_BITSTUFF | \
                                UHCI_TD_STS_CRC_TIMEOUT | \
                                UHCI_TD_STS_NAK | \
                                UHCI_TD_STS_BABBLE | \
                                UHCI_TD_STS_DATABUFFER | \
                                UHCI_TD_STS_STALLED | \
                                UHCI_TD_STS_RESERVED16)
#define UHCI_TD_STS_FATAL      (UHCI_TD_STS_BITSTUFF | \
                                UHCI_TD_STS_CRC_TIMEOUT | \
                                UHCI_TD_STS_BABBLE | \
                                UHCI_TD_STS_DATABUFFER | \
                                UHCI_TD_STS_STALLED)

#define UHCI_PID_OUT   0xE1U
#define UHCI_PID_IN    0x69U
#define UHCI_PID_SETUP 0x2DU

#define CBW_SIGNATURE 0x43425355U
#define CSW_SIGNATURE 0x53425355U
#define MSC_CBW_WIRE_SIZE 31U
#define MSC_CSW_WIRE_SIZE 13U

#define SCSI_TEST_UNIT_READY 0x00
#define SCSI_REQUEST_SENSE   0x03
#define SCSI_INQUIRY         0x12
#define SCSI_READ_CAPACITY10 0x25
#define SCSI_READ10          0x28
#define SCSI_WRITE10         0x2A

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

/* UHCI hardware transfer descriptor: 16-byte hardware prefix, 16-byte SW. */
typedef struct uhci_td {
    volatile uint32_t link;
    volatile uint32_t status;
    volatile uint32_t token;
    volatile uint32_t buffer;
    uint32_t next;
    uint32_t used;
    uint8_t pad[8];
} PACKED uhci_td_t;

/* UHCI queue head: first 8 bytes are hardware-owned. */
typedef struct {
    volatile uint32_t head;
    volatile uint32_t element;
    uint32_t used;
    uint32_t pad;
} PACKED uhci_qh_t;

typedef struct {
    uint32_t completed_tds;
    uint32_t actual_bytes;
    uint32_t failed_status;
    uint32_t failed_token;
    uint8_t failed_toggle;
    uint8_t failed_endpoint;
    uint8_t failed_pid;
    bool short_packet;
    bool failed_valid;
} uhci_chain_result_t;

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
    uint16_t io_base;
    uint8_t port_count;
    uint32_t *frame_list;
    uhci_qh_t *qh;
    uhci_qh_t *xfer_qh;
    uhci_td_t *term_td;
    uhci_td_t *td_pool;
    uint8_t *setup_buffer;
    uint8_t *control_buffer;
    uint8_t swdbg_done_mask;
    uint8_t low_speed_probe_mask;
    bool ready;
} uhci_controller_t;

typedef struct {
    uhci_controller_t *hc;
    uint8_t addr;
    uint8_t max_packet0;
    uint8_t bulk_in;
    uint8_t bulk_out;
    uint8_t interface_number;
    uint8_t interface_alternate;
    uint8_t max_lun;
    uint16_t bulk_in_packet;
    uint16_t bulk_out_packet;
    uint8_t toggle_in;
    uint8_t toggle_out;
    uint32_t sectors;
    uint32_t tag;
    uint8_t last_csw_status;
    bool registered;
    char name[8];
} uhci_mass_device_t;

/* Used by the enumeration retry path before its full definition below. */
static bool uhci_reset_port(uhci_controller_t *hc, uint32_t port);
static void uhci_ack_port_changes(uhci_controller_t *hc, uint32_t port);
static uint16_t uhci_port_addr(uhci_controller_t *hc, uint32_t port);
static void uhci_log_port_state(uhci_controller_t *hc, const char *label);

static uhci_controller_t g_uhci[UHCI_MAX_CONTROLLERS];
static uhci_mass_device_t g_mass[UHCI_MAX_CONTROLLERS];
static uint32_t g_uhci_count = 0;
static uint32_t g_uhci_fail_log_budget = 32;
static uint32_t g_uhci_bot_trace_budget = 12;
static uint32_t g_uhci_td_trace_budget = 24;

/*
 * Real PIIX4 hardware is less forgiving than QEMU about schedule memory.
 * Keep every UHCI hardware-visible object in a statically allocated,
 * identity-mapped low-memory DMA arena.  This avoids heap metadata,
 * accidental reuse and any ambiguity between a C pointer and the physical
 * address fetched by the PCI bus master.
 *
 * Each frame-list row is exactly 4096 bytes, so all controller rows remain
 * 4 KiB aligned.  TD rows are 4096 bytes as well (128 * 32 bytes).
 */
static uint32_t g_uhci_frame_lists[UHCI_MAX_CONTROLLERS][UHCI_FRAME_COUNT]
    __attribute__((aligned(UHCI_FRAME_ALIGN)));
static uhci_qh_t g_uhci_qhs[UHCI_MAX_CONTROLLERS]
    __attribute__((aligned(UHCI_QH_ALIGN)));
static uhci_qh_t g_uhci_xfer_qhs[UHCI_MAX_CONTROLLERS]
    __attribute__((aligned(UHCI_QH_ALIGN)));
static uhci_td_t g_uhci_term_tds[UHCI_MAX_CONTROLLERS]
    __attribute__((aligned(UHCI_TD_ALIGN)));
static uhci_td_t g_uhci_td_pools[UHCI_MAX_CONTROLLERS][UHCI_MAX_TD]
    __attribute__((aligned(UHCI_TD_ALIGN)));
static uint8_t g_uhci_setup_buffers[UHCI_MAX_CONTROLLERS][16]
    __attribute__((aligned(16)));
static uint8_t g_uhci_control_buffers[UHCI_MAX_CONTROLLERS][UHCI_CTRL_DMA_SIZE]
    __attribute__((aligned(64)));

static inline uint16_t uhci_inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void uhci_outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void uhci_barrier(void) {
    __asm__ volatile ("" : : : "memory");
}

/*
 * Publish descriptor and data-buffer writes before the PIIX4 can fetch them.
 * A compiler barrier alone is enough in QEMU, but it does not force pending
 * CPU stores to become globally visible to a real PCI bus master.  The locked
 * no-op is valid on the CPUs targeted by BlesKernOS and acts as a full fence.
 */
static inline void uhci_dma_fence(void) {
    __asm__ volatile ("lock; addl $0, 0(%%esp)" : : : "memory", "cc");
}

static uint32_t uhci_terminator_link(const uhci_controller_t *hc) {
    if (!hc || !hc->term_td) return UHCI_LINK_TERM;
    return (uint32_t)(uintptr_t)hc->term_td;
}

/*
 * Linux, Haiku and the BSD UHCI drivers all keep an inactive stray TD at
 * the end of the asynchronous schedule for an Intel PIIX hardware bug.
 * A raw T=1 horizontal end is valid UHCI, but it is not sufficient for all
 * PIIX implementations.  Parking both QHs on the same immutable TD also
 * prevents the controller from observing a transient empty schedule while
 * software publishes or detaches a transfer queue.
 */
static void uhci_park_schedule(uhci_controller_t *hc) {
    uint32_t term;
    if (!hc) return;
    term = uhci_terminator_link(hc);
    if (hc->qh) {
        hc->qh->head = term;
        hc->qh->element = UHCI_LINK_TERM;
    }
    if (hc->xfer_qh) {
        hc->xfer_qh->head = term;
        hc->xfer_qh->element = UHCI_LINK_TERM;
    }
}

static inline void usb_io_delay(void) {
    outb(0x80, 0);
}

static void usb_delay_ms(uint32_t ms) {
    uint32_t flags;
    uint32_t hz = pit_get_frequency_hz();

    __asm__ volatile ("pushfl; popl %0" : "=r"(flags));
    if ((flags & (1U << 9)) && hz) {
        uint32_t start = pit_get_ticks();
        uint32_t ticks =
            (uint32_t)(((uint64_t)ms * hz + 999U) / 1000U);
        if (!ticks && ms) ticks = 1;
        while ((uint32_t)(pit_get_ticks() - start) < ticks)
            __asm__ volatile ("pause");
        return;
    }

    /* Early-boot fallback for callers running with timer IRQs disabled. */
    for (uint32_t m = 0; m < ms; m++) {
        for (uint32_t i = 0; i < 12000; i++) usb_io_delay();
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

static void wr32be(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void wr32le(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uhci_td_t *uhci_alloc_td(uhci_controller_t *hc) {
    for (uint32_t i = 0; i < UHCI_MAX_TD; i++) {
        if (!hc->td_pool[i].used) {
            kmemset(&hc->td_pool[i], 0, sizeof(hc->td_pool[i]));
            hc->td_pool[i].used = true;
            hc->td_pool[i].link = UHCI_LINK_TERM;
            return &hc->td_pool[i];
        }
    }
    return NULL;
}

static void uhci_free_chain(uhci_td_t *td) {
    while (td) {
        uhci_td_t *next = (uhci_td_t *)(uintptr_t)td->next;
        td->used = false;
        td = next;
    }
}

static uint32_t uhci_token(uint8_t pid, uint8_t addr, uint8_t endpoint,
                           uint8_t toggle, uint32_t len) {
    uint32_t encoded_len = len ? len - 1U : 0x7FFU;
    return (uint32_t)pid |
           ((uint32_t)(addr & 0x7FU) << 8) |
           ((uint32_t)(endpoint & 0x0FU) << 15) |
           ((uint32_t)(toggle & 1U) << 19) |
           ((encoded_len & 0x7FFU) << 21);
}

static uint32_t uhci_make_status(bool low_speed, bool ioc, bool spd) {
    uint32_t status = UHCI_TD_STS_ACTIVE |
                      UHCI_TD_STS_ERRCNT(3);
    if (low_speed) status |= UHCI_TD_STS_LOW_SPEED;
    if (ioc) status |= UHCI_TD_STS_IOC;
    if (spd) status |= UHCI_TD_STS_SPD;
    return status;
}

static uint32_t uhci_td_expected_length(uint32_t token);

static bool uhci_validate_initial_td(const uhci_td_t *td,
                                     const char *stage,
                                     uint32_t index) {
    uint32_t status;

    if (!td) return false;
    status = td->status;
    if ((status & UHCI_TD_ACTLEN_MASK) == 0U &&
        (status & UHCI_TD_STS_ERRCNT_MASK) == UHCI_TD_STS_ERRCNT(3) &&
        (status & UHCI_TD_STS_ACTIVE) &&
        !(status & UHCI_TD_STS_INITIAL_BAD))
        return true;

    kprintf("  [USB-FATAL] TD inicial invalido %s td%u sts=%x token=%x buf=%x\n",
            stage ? stage : "?", index, status, td->token, td->buffer);
    kprintf("  [USB-FATAL]   active=%u cerr=%u actraw=%x bad=%x\n",
            (status & UHCI_TD_STS_ACTIVE) != 0,
            (status & UHCI_TD_STS_ERRCNT_MASK) >> 27,
            status & UHCI_TD_ACTLEN_MASK,
            status & UHCI_TD_STS_INITIAL_BAD);
    return false;
}

static void uhci_trace_td_status(const char *when, const char *stage,
                                 const uhci_td_t *td) {
    uint32_t status;
    uint32_t token;

    if (!g_uhci_td_trace_budget || !td) return;
    status = td->status;
    token = td->token;
    kprintf("  [USB-TD] %s %s td=%x sts=%x token=%x buf=%x\n",
            when ? when : "?", stage ? stage : "?",
            (uint32_t)(uintptr_t)td, status, token, td->buffer);
    kprintf("  [USB-TD]   pid=%x addr=%u ep=%u tog=%u exp=%u actraw=%x active=%u cerr=%u\n",
            token & 0xFFU, (token >> 8) & 0x7FU,
            (token >> 15) & 0x0FU, (token >> 19) & 1U,
            uhci_td_expected_length(token),
            status & UHCI_TD_ACTLEN_MASK,
            (status & UHCI_TD_STS_ACTIVE) != 0,
            (status & UHCI_TD_STS_ERRCNT_MASK) >> 27);
    kprintf("  [USB-TD]   crc=%u stalled=%u nak=%u bitstuff=%u babble=%u dbuf=%u bad16=%u\n",
            (status & UHCI_TD_STS_CRC_TIMEOUT) != 0,
            (status & UHCI_TD_STS_STALLED) != 0,
            (status & UHCI_TD_STS_NAK) != 0,
            (status & UHCI_TD_STS_BITSTUFF) != 0,
            (status & UHCI_TD_STS_BABBLE) != 0,
            (status & UHCI_TD_STS_DATABUFFER) != 0,
            (status & UHCI_TD_STS_RESERVED16) != 0);
    g_uhci_td_trace_budget--;
}

static void uhci_td_init(uhci_td_t *td, uhci_td_t *prev, uint8_t pid,
                         uint8_t addr, uint8_t endpoint, uint8_t toggle,
                         void *buffer, uint32_t len, bool low_speed) {
    if (prev) {
        /*
         * UHCI 1.1, section 3.2.1: depth-first is a valid way to continue
         * vertically through this queue.  It keeps SETUP, DATA and STATUS
         * together when frame time remains; breadth-first would also be
         * conforming, but would revisit the QH between transactions.
         */
        prev->link = (uint32_t)(uintptr_t)td | UHCI_LINK_DEPTH;
        prev->next = (uint32_t)(uintptr_t)td;
    }
    td->link = UHCI_LINK_TERM;
    td->next = 0;
    /* ActLen is a host-controller result.  Linux UHCI and Haiku initialize
       it to zero while ACTIVE and only decode the n-1 value after writeback.
       Do not preseed it with the zero-length result (0x7ff): on old PIIX
       silicon the field is hardware-owned as soon as ACTIVE is published. */
    td->status = uhci_make_status(low_speed, false, false);
    td->token = uhci_token(pid, addr, endpoint, toggle, len);
    td->buffer = len ? (uint32_t)(uintptr_t)buffer : 0;
}

static bool uhci_chain_active(uhci_td_t *head) {
    for (uhci_td_t *td = head; td;
         td = (uhci_td_t *)(uintptr_t)td->next) {
        if (td->status & UHCI_TD_STS_ACTIVE) return true;
    }
    return false;
}

static bool uhci_chain_failed(uhci_td_t *head) {
    for (uhci_td_t *td = head; td;
         td = (uhci_td_t *)(uintptr_t)td->next) {
        if (!(td->status & UHCI_TD_STS_ACTIVE) &&
            (td->status & UHCI_TD_STS_FATAL))
            return true;
    }
    return false;
}

static bool uhci_chain_ok(uhci_td_t *head) {
    for (uhci_td_t *td = head; td;
         td = (uhci_td_t *)(uintptr_t)td->next) {
        if (td->status & (UHCI_TD_STS_ACTIVE | UHCI_TD_STS_FATAL))
            return false;
    }
    return true;
}

static uint32_t uhci_td_expected_length(uint32_t token) {
    uint32_t encoded = (token >> 21) & 0x7FFU;
    return (encoded + 1U) & 0x7FFU;
}

static uint32_t uhci_td_actual_length(uint32_t status) {
    return (status + 1U) & 0x7FFU;
}

static bool uhci_td_is_short_in(const uhci_td_t *td) {
    uint32_t status;
    uint32_t expected;

    if (!td) return false;
    status = td->status;
    if (status & (UHCI_TD_STS_ACTIVE | UHCI_TD_STS_FATAL)) return false;
    if (!(status & UHCI_TD_STS_SPD) ||
        (td->token & 0xFFU) != UHCI_PID_IN)
        return false;
    expected = uhci_td_expected_length(td->token);
    return uhci_td_actual_length(status) < expected;
}

/* A QH deliberately does not advance past an SPD short packet.  Only scan
   the consecutive completed prefix: later TDs still belong to hardware's
   unexecuted tail and must not be counted for bytes or toggle parity. */
static bool uhci_chain_has_short_in(uhci_td_t *head) {
    for (uhci_td_t *td = head; td;
         td = (uhci_td_t *)(uintptr_t)td->next) {
        if (td->status & (UHCI_TD_STS_ACTIVE | UHCI_TD_STS_FATAL))
            return false;
        if (uhci_td_is_short_in(td)) return true;
    }
    return false;
}

static void uhci_chain_set_active(uhci_td_t *head, bool active) {
    for (uhci_td_t *td = head; td;
         td = (uhci_td_t *)(uintptr_t)td->next) {
        if (active) {
            uint32_t keep = td->status & (UHCI_TD_STS_LOW_SPEED |
                                          UHCI_TD_STS_IOC |
                                          UHCI_TD_STS_SPD);
            td->status = UHCI_TD_STS_ACTIVE |
                         UHCI_TD_STS_ERRCNT(3) |
                         keep;
        } else {
            td->status &= (uint32_t)~UHCI_TD_STS_ACTIVE;
        }
    }
}

static void uhci_chain_collect(uhci_td_t *head,
                               uhci_chain_result_t *result) {
    if (!result) return;
    kmemset(result, 0, sizeof(*result));
    for (uhci_td_t *td = head; td;
         td = (uhci_td_t *)(uintptr_t)td->next) {
        uint32_t status = td->status;
        uint32_t token = td->token;

        if (!(status & UHCI_TD_STS_ACTIVE))
            result->actual_bytes += uhci_td_actual_length(status);
        if (status & (UHCI_TD_STS_ACTIVE | UHCI_TD_STS_FATAL)) {
            result->failed_valid = true;
            result->failed_status = status;
            result->failed_token = token;
            result->failed_pid = (uint8_t)(token & 0xFFU);
            result->failed_endpoint = (uint8_t)((token >> 15) & 0x0FU);
            result->failed_toggle = (uint8_t)((token >> 19) & 1U);
            break;
        }
        result->completed_tds++;
        if (uhci_td_is_short_in(td)) {
            result->short_packet = true;
            break;
        }
    }
}

static void usb_hex_line(char *dst, const uint8_t *src, uint32_t count);

static void uhci_dump_setup(const uhci_controller_t *hc) {
    const uint8_t *b;
    char line[24];
    if (!hc || !hc->setup_buffer) return;
    b = hc->setup_buffer;
    usb_hex_line(line, b, 8);
    kprintf("  [USB-DIAG] setup=%x bytes=%s\n",
            (uint32_t)(uintptr_t)b, line);
}

static void uhci_log_chain_error(uhci_controller_t *hc, uhci_td_t *head,
                                 uint32_t host_status,
                                 const char *stage) {
    uint16_t cmd;
    uint16_t frnum;
    uint32_t flbase;
    uint32_t frame_entry = 0;
    uint32_t expected_frame = 0;
    uint32_t index = 0;

    if (!g_uhci_fail_log_budget || !hc) return;
    cmd = uhci_inw((uint16_t)(hc->io_base + UHCI_USBCMD));
    frnum = uhci_inw((uint16_t)(hc->io_base + UHCI_FRNUM));
    flbase = inl((uint16_t)(hc->io_base + UHCI_FLBASEADD));
    if (hc->frame_list) {
        frame_entry = hc->frame_list[frnum & (UHCI_FRAME_COUNT - 1U)];
        expected_frame = (uint32_t)(uintptr_t)hc->qh | UHCI_LINK_QH;
    }

    kprintf("  [USB-DIAG] stage=%s cmd=%x sts=%x fr=%u flbase=%x\n",
            stage ? stage : "?", cmd, host_status, frnum, flbase);
    kprintf("  [USB-DIAG] qh=%x head=%x elem=%x frame[%u]=%x\n",
            (uint32_t)(uintptr_t)hc->qh,
            hc->qh ? hc->qh->head : 0,
            hc->qh ? hc->qh->element : 0,
            frnum & (UHCI_FRAME_COUNT - 1U), frame_entry);
    kprintf("  [USB-DIAG] xfer-qh=%x head=%x elem=%x\n",
            (uint32_t)(uintptr_t)hc->xfer_qh,
            hc->xfer_qh ? hc->xfer_qh->head : 0,
            hc->xfer_qh ? hc->xfer_qh->element : 0);
    kprintf("  [USB-SCHED] legal=%u frame=%x/%x qh-head=%x/%x xqh-head=%x/%x\n",
            frame_entry == expected_frame &&
                hc->qh && hc->xfer_qh &&
                hc->qh->head ==
                    ((uint32_t)(uintptr_t)hc->xfer_qh | UHCI_LINK_QH) &&
                hc->xfer_qh->head == uhci_terminator_link(hc),
            frame_entry, expected_frame,
            hc->qh ? hc->qh->head : 0,
            (uint32_t)(uintptr_t)hc->xfer_qh | UHCI_LINK_QH,
            hc->xfer_qh ? hc->xfer_qh->head : 0,
            uhci_terminator_link(hc));
    uhci_dump_setup(hc);

    for (uhci_td_t *td = head; td && index < UHCI_DIAG_MAX_TDS;
         td = (uhci_td_t *)(uintptr_t)td->next, index++) {
        uint32_t token = td->token;
        uint32_t status = td->status;
        bool act_valid = !(status & UHCI_TD_STS_ACTIVE) ||
                         (status & UHCI_TD_STS_ERRCNT_MASK) !=
                             UHCI_TD_STS_ERRCNT(3) ||
                         (status & (UHCI_TD_STS_NAK |
                                    UHCI_TD_STS_FATAL));
        kprintf("  [USB-DIAG] td%u=%x link=%x sts=%x token=%x buf=%x\n",
                index, (uint32_t)(uintptr_t)td, td->link, status,
                token, td->buffer);
        kprintf("  [USB-DIAG]   pid=%x addr=%u ep=%u tog=%u exp=%u act=%u valid=%u flags=%x\n",
                token & 0xFFU, (token >> 8) & 0x7FU,
                (token >> 15) & 0x0FU, (token >> 19) & 1U,
                uhci_td_expected_length(token),
                uhci_td_actual_length(status),
                act_valid,
                status & 0x3FFE0000U);
        kprintf("  [USB-DIAG]   cerr=%u actraw=%x\n",
                (status & UHCI_TD_STS_ERRCNT_MASK) >> 27,
                status & UHCI_TD_ACTLEN_MASK);
        kprintf("  [USB-DIAG]   active=%u nak=%u crc=%u stalled=%u bitstuff=%u babble=%u dbuf=%u\n",
                (status & UHCI_TD_STS_ACTIVE) != 0,
                (status & UHCI_TD_STS_NAK) != 0,
                (status & UHCI_TD_STS_CRC_TIMEOUT) != 0,
                (status & UHCI_TD_STS_STALLED) != 0,
                (status & UHCI_TD_STS_BITSTUFF) != 0,
                (status & UHCI_TD_STS_BABBLE) != 0,
                (status & UHCI_TD_STS_DATABUFFER) != 0);
    }
    g_uhci_fail_log_budget--;
}

static bool uhci_wait_frame_advance(uhci_controller_t *hc,
                                    uint16_t start, uint32_t frames) {
    uint32_t timeout = 2000000U;
    if (!hc || !frames) return true;
    while (timeout--) {
        uint16_t now = uhci_inw((uint16_t)(hc->io_base + UHCI_FRNUM));
        uint16_t elapsed = (uint16_t)((now - start) & 0x07FFU);
        if (elapsed >= frames) return true;
        __asm__ volatile ("pause");
    }
    return false;
}

static bool uhci_wait_halted(uhci_controller_t *hc, bool halted,
                             uint32_t timeout) {
    if (!hc) return false;
    while (timeout) {
        bool is_halted =
            (uhci_inw((uint16_t)(hc->io_base + UHCI_USBSTS)) &
             UHCI_STS_HALTED) != 0;
        if (is_halted == halted) return true;
        timeout--;
        __asm__ volatile ("pause");
    }
    return false;
}

static bool uhci_run_chain(uhci_controller_t *hc, uhci_td_t *head,
                           bool log_errors, const char *stage,
                           uint32_t timeout_ms,
                           uhci_chain_result_t *result) {
    uint16_t host_status = 0;
    uint16_t detach_frame;
    uint16_t last_frame;
    uint32_t elapsed_ms = 0;
    uint32_t frame_stall_guard = UHCI_FRAME_STALL_GUARD;
    bool timed_out = false;
    bool stopped_for_abort = false;
    bool stop_on_short = result != NULL;
    bool short_packet = false;
    bool ok;

    if (result) kmemset(result, 0, sizeof(*result));

    if (!hc || !hc->ready || !hc->qh || !hc->xfer_qh || !head) {
        if (head) uhci_free_chain(head);
        return false;
    }

    /*
     * Publish a complete TD chain at once.  A USB control transfer is one
     * ordered SETUP/DATA/STATUS operation; submitting each stage as an
     * independent QH run works in QEMU but is needlessly fragile on PIIX4.
     */
    if (((uint32_t)(uintptr_t)head & (UHCI_TD_ALIGN - 1U)) != 0U ||
        (uint32_t)(uintptr_t)head >= 0x01000000U) {
        kprintf("  [USB-DIAG] TD DMA invalido head=%x\n",
                (uint32_t)(uintptr_t)head);
        uhci_free_chain(head);
        return false;
    }

    uhci_outw((uint16_t)(hc->io_base + UHCI_USBSTS), UHCI_STS_CLEAR);
    (void)uhci_inw((uint16_t)(hc->io_base + UHCI_USBSTS));
    uhci_park_schedule(hc);
    uhci_chain_set_active(head, false);
    uhci_dma_fence();
    uhci_chain_set_active(head, true);
    uhci_dma_fence();
    {
        uint32_t index = 0;
        for (uhci_td_t *td = head; td;
             td = (uhci_td_t *)(uintptr_t)td->next, index++) {
            if (!uhci_validate_initial_td(td, stage, index)) {
                uhci_free_chain(head);
                return false;
            }
        }
    }
    uhci_trace_td_status("before", stage, head);
    hc->xfer_qh->element = (uint32_t)(uintptr_t)head;
    uhci_dma_fence();
    hc->qh->head = (uint32_t)(uintptr_t)hc->xfer_qh | UHCI_LINK_QH;
    uhci_dma_fence();

    if (!timeout_ms) timeout_ms = 1;
    last_frame = uhci_inw((uint16_t)(hc->io_base + UHCI_FRNUM)) & 0x07FFU;
    while (uhci_chain_active(head) && !uhci_chain_failed(head) &&
           !(stop_on_short && uhci_chain_has_short_in(head))) {
        uint16_t now;
        uint16_t delta;

        host_status = uhci_inw((uint16_t)(hc->io_base + UHCI_USBSTS));
        if (host_status & (UHCI_STS_HSE | UHCI_STS_HCPE |
                           UHCI_STS_HALTED))
            break;

        now = uhci_inw((uint16_t)(hc->io_base + UHCI_FRNUM)) & 0x07FFU;
        delta = (uint16_t)((now - last_frame) & 0x07FFU);
        if (delta) {
            elapsed_ms += delta;
            last_frame = now;
            frame_stall_guard = UHCI_FRAME_STALL_GUARD;
        } else if (frame_stall_guard) {
            frame_stall_guard--;
        }

        if (elapsed_ms >= timeout_ms || !frame_stall_guard) {
            timed_out = true;
            break;
        }
        __asm__ volatile ("pause");
    }

    uhci_dma_fence();
    host_status = uhci_inw((uint16_t)(hc->io_base + UHCI_USBSTS));
    uhci_trace_td_status("after", stage, head);
    short_packet = stop_on_short && uhci_chain_has_short_in(head);
    ok = !timed_out &&
         !(host_status & (UHCI_STS_HSE | UHCI_STS_HCPE |
                          UHCI_STS_HALTED)) &&
         (short_packet || uhci_chain_ok(head));

    /* A still-active TD may already be inside the PIIX4 when the real-time
       frame timeout expires.  Stop the HC before detaching or recycling it. */
    if (timed_out && uhci_chain_active(head)) {
        uhci_outw((uint16_t)(hc->io_base + UHCI_USBCMD),
                  UHCI_CMD_CF | UHCI_CMD_MAXP);
        stopped_for_abort = uhci_wait_halted(hc, true, 2000000U);
        if (!stopped_for_abort) {
            if (log_errors)
                uhci_log_chain_error(hc, head, host_status,
                                     "ABORT-NO-HALT");
            /* Hardware may still own this chain; retain it permanently. */
            hc->ready = false;
            return false;
        }
    }

    if (!ok && log_errors) {
        if (timed_out)
            kprintf("  [USB-DIAG] timeout %s tras %u ms (FRNUM)\n",
                    stage ? stage : "?", elapsed_ms);
        uhci_log_chain_error(hc, head, host_status, stage);
    }

    detach_frame = uhci_inw((uint16_t)(hc->io_base + UHCI_FRNUM));
    uhci_park_schedule(hc);
    uhci_dma_fence();
    if (!stopped_for_abort &&
        !uhci_wait_frame_advance(hc, detach_frame, 2)) {
        uhci_outw((uint16_t)(hc->io_base + UHCI_USBCMD),
                  UHCI_CMD_CF | UHCI_CMD_MAXP);
        stopped_for_abort = uhci_wait_halted(hc, true, 2000000U);
        if (!stopped_for_abort) {
            kprintf("  [USB-DIAG] HC no libero cadena; TDs retenidos\n");
            hc->ready = false;
            return false;
        }
        /* Reassert after any write-back completed while stopping. */
        uhci_park_schedule(hc);
        uhci_dma_fence();
    }
    uhci_outw((uint16_t)(hc->io_base + UHCI_USBSTS), UHCI_STS_CLEAR);

    uhci_chain_collect(head, result);
    uhci_free_chain(head);
    if (stopped_for_abort) {
        uhci_outw((uint16_t)(hc->io_base + UHCI_USBCMD), UHCI_CMD_RUN);
        if (!uhci_wait_halted(hc, false, 2000000U)) {
            kprintf("  [USB-DIAG] HC no reinicio tras abortar cadena\n");
            hc->ready = false;
        }
    }
    return ok;
}

static void uhci_encode_setup(uint8_t *dst, const usb_setup_t *setup) {
    dst[0] = setup->type;
    dst[1] = setup->req;
    dst[2] = (uint8_t)(setup->value & 0xFFU);
    dst[3] = (uint8_t)(setup->value >> 8);
    dst[4] = (uint8_t)(setup->index & 0xFFU);
    dst[5] = (uint8_t)(setup->index >> 8);
    dst[6] = (uint8_t)(setup->len & 0xFFU);
    dst[7] = (uint8_t)(setup->len >> 8);
}

/* Execute one SETUP with every frame-list entry pointing directly at its TD.
   This deliberately bypasses both QHs and every TD link traversal so a real
   hardware trace can distinguish a malformed queue from a bus handshake
   failure. The normal schedule is restored while the HC is stopped. */
static bool uhci_debug_setup_direct_frame(uhci_controller_t *hc,
                                          const usb_setup_t *setup,
                                          bool low_speed) {
    uhci_td_t *td;
    uint16_t start_frame;
    uint16_t last_frame;
    uint16_t host_status;
    uint32_t elapsed = 0;
    uint32_t guard = UHCI_FRAME_STALL_GUARD;
    uint32_t status;
    bool ack;

    if (!hc || !hc->ready || !setup) return false;
    td = uhci_alloc_td(hc);
    if (!td) return false;

    uhci_encode_setup(hc->setup_buffer, setup);
    uhci_td_init(td, NULL, UHCI_PID_SETUP, 0, 0, 0,
                 hc->setup_buffer, sizeof(*setup), low_speed);
    uhci_dma_fence();

    uhci_outw((uint16_t)(hc->io_base + UHCI_USBCMD),
              UHCI_CMD_CF | UHCI_CMD_MAXP);
    if (!uhci_wait_halted(hc, true, 2000000U)) {
        kprintf("  [USB-DIRECT] no se pudo detener HC\n");
        uhci_free_chain(td);
        return false;
    }

    uhci_park_schedule(hc);
    for (uint32_t frame = 0; frame < UHCI_FRAME_COUNT; frame++)
        hc->frame_list[frame] = (uint32_t)(uintptr_t)td;
    uhci_dma_fence();
    uhci_outw((uint16_t)(hc->io_base + UHCI_USBSTS), UHCI_STS_CLEAR);
    uhci_outw((uint16_t)(hc->io_base + UHCI_USBCMD), UHCI_CMD_RUN);

    start_frame = uhci_inw((uint16_t)(hc->io_base + UHCI_FRNUM)) & 0x07FFU;
    last_frame = start_frame;
    while ((td->status & UHCI_TD_STS_ACTIVE) && elapsed < 50U && guard) {
        uint16_t now =
            uhci_inw((uint16_t)(hc->io_base + UHCI_FRNUM)) & 0x07FFU;
        uint16_t delta = (uint16_t)((now - last_frame) & 0x07FFU);
        if (delta) {
            elapsed += delta;
            last_frame = now;
            guard = UHCI_FRAME_STALL_GUARD;
        } else {
            guard--;
        }
        __asm__ volatile ("pause");
    }

    uhci_dma_fence();
    status = td->status;
    host_status = uhci_inw((uint16_t)(hc->io_base + UHCI_USBSTS));
    ack = !(status & (UHCI_TD_STS_ACTIVE | UHCI_TD_STS_FATAL));
    kprintf("  [USB-DIRECT] QH-bypass ack=%u td=%x sts=%x token=%x frames=%u host=%x cmd=%x\n",
            ack, (uint32_t)(uintptr_t)td, status, td->token,
            (last_frame - start_frame) & 0x07FFU, host_status,
            uhci_inw((uint16_t)(hc->io_base + UHCI_USBCMD)));

    uhci_outw((uint16_t)(hc->io_base + UHCI_USBCMD),
              UHCI_CMD_CF | UHCI_CMD_MAXP);
    if (!uhci_wait_halted(hc, true, 2000000U)) {
        kprintf("  [USB-DIRECT] HC no libero TD; schedule no restaurado\n");
        hc->ready = false;
        return false;
    }

    for (uint32_t frame = 0; frame < UHCI_FRAME_COUNT; frame++)
        hc->frame_list[frame] =
            (uint32_t)(uintptr_t)hc->qh | UHCI_LINK_QH;
    uhci_park_schedule(hc);
    uhci_dma_fence();
    uhci_free_chain(td);
    uhci_outw((uint16_t)(hc->io_base + UHCI_USBSTS), UHCI_STS_CLEAR);
    uhci_outw((uint16_t)(hc->io_base + UHCI_USBCMD), UHCI_CMD_RUN);
    if (!uhci_wait_halted(hc, false, 2000000U)) {
        kprintf("  [USB-DIRECT] HC no reinicio tras restaurar schedule\n");
        hc->ready = false;
    }
    return ack;
}

static bool uhci_validate_setup_dma(const uint8_t *b,
                                    const usb_setup_t *setup,
                                    const char *stage) {
    uint8_t expected[8];
    char have[24];
    char want[24];

    if (!b || !setup) return false;
    expected[0] = setup->type;
    expected[1] = setup->req;
    expected[2] = (uint8_t)(setup->value & 0xFFU);
    expected[3] = (uint8_t)(setup->value >> 8);
    expected[4] = (uint8_t)(setup->index & 0xFFU);
    expected[5] = (uint8_t)(setup->index >> 8);
    expected[6] = (uint8_t)(setup->len & 0xFFU);
    expected[7] = (uint8_t)(setup->len >> 8);

    for (uint32_t i = 0; i < sizeof(expected); i++) {
        if (b[i] != expected[i]) {
            usb_hex_line(have, b, sizeof(expected));
            usb_hex_line(want, expected, sizeof(expected));
            kprintf("  [USB-FATAL] SETUP DMA corrupto %s have=%s want=%s\n",
                    stage ? stage : "?", have, want);
            return false;
        }
    }
    return true;
}

static bool uhci_run_control_stage(uhci_controller_t *hc, uint8_t pid,
                                   uint8_t addr, uint8_t toggle,
                                   void *buffer, uint32_t len,
                                   bool low_speed, const char *stage,
                                   bool allow_short,
                                   uint32_t *actual_len) {
    uhci_td_t *td = uhci_alloc_td(hc);
    uhci_chain_result_t result;

    if (actual_len) *actual_len = 0;
    if (!td) return false;
    uhci_td_init(td, NULL, pid, addr, 0, toggle, buffer, len, low_speed);
    if (allow_short) td->status |= UHCI_TD_STS_SPD;
    if (!uhci_run_chain(hc, td, true, stage, UHCI_CONTROL_TIMEOUT_MS,
                        &result))
        return false;
    if (actual_len) *actual_len = result.actual_bytes;
    return result.completed_tds == 1U;
}

/* Diagnostic only: issue one SETUP at a candidate address without DATA or
   STATUS. A hit is followed immediately by a port reset, so the deliberately
   incomplete control transfer cannot leak into normal enumeration. */
static bool uhci_probe_setup_address_quiet(uhci_controller_t *hc,
                                           uint8_t addr,
                                           const usb_setup_t *setup,
                                           bool low_speed) {
    uhci_td_t *td;
    uhci_chain_result_t result;

    if (!hc || !setup) return false;
    td = uhci_alloc_td(hc);
    if (!td) return false;
    uhci_encode_setup(hc->setup_buffer, setup);
    uhci_dma_fence();
    uhci_td_init(td, NULL, UHCI_PID_SETUP, addr, 0, 0,
                 hc->setup_buffer, sizeof(*setup), low_speed);
    if (!uhci_run_chain(hc, td, false, "ADDR-PROBE", 20U, &result))
        return false;
    return result.completed_tds == 1U;
}

static bool uhci_control_split_in(uhci_controller_t *hc, uint8_t addr,
                                  uint8_t max_packet, usb_setup_t *setup,
                                  void *data, uint32_t len,
                                  bool low_speed) {
    uint32_t offset = 0;
    uint32_t actual = 0;
    uint8_t toggle = 1;

    if (!hc || !setup || !max_packet || !data || !len) return false;

    /*
     * Fallback for old PIIX4M/BIOS handoff cases where a full depth-first
     * control chain fails before BOT starts.  Keep it to IN requests so
     * retrying cannot double-apply state-changing requests such as SET_ADDRESS.
     */
    kprintf("  [USB-DIAG] EP0 por etapas type=%x req=%x len=%u\n",
            setup->type, setup->req, setup->len);

    uhci_encode_setup(hc->setup_buffer, setup);
    uhci_dma_fence();
    if (!uhci_validate_setup_dma(hc->setup_buffer, setup, "CTRL-SETUP"))
        return false;
    if (!uhci_run_control_stage(hc, UHCI_PID_SETUP, addr, 0,
                                hc->setup_buffer, sizeof(usb_setup_t),
                                low_speed, "CTRL-SETUP", false, NULL))
        return false;

    while (offset < len) {
        uint32_t chunk = len - offset;
        bool final_packet;
        bool allow_short;
        if (chunk > max_packet) chunk = max_packet;
        final_packet = offset + chunk >= len;
        allow_short = !final_packet;
        if (!uhci_run_control_stage(hc, UHCI_PID_IN, addr, toggle,
                                    hc->control_buffer + offset, chunk,
                                    low_speed, "CTRL-DATA-IN",
                                    allow_short, &actual))
            return false;
        if (actual > chunk) return false;
        offset += actual;
        toggle ^= 1U;
        if (actual < chunk) break;
    }

    if (!uhci_run_control_stage(hc, UHCI_PID_OUT, addr, 1,
                                NULL, 0, low_speed,
                                "CTRL-STATUS-OUT", false, NULL))
        return false;

    uhci_dma_fence();
    kmemcpy(data, hc->control_buffer, offset);
    return true;
}

static bool uhci_control(uhci_controller_t *hc, uint8_t addr,
                         uint8_t max_packet, usb_setup_t *setup,
                         void *data, uint32_t len, bool low_speed,
                         bool log_errors) {
    uhci_td_t *head;
    uhci_td_t *prev;
    uhci_td_t *td;
    uint8_t pid;
    uint8_t toggle = 1;
    uint32_t offset = 0;
    bool in;

    if (!hc || !setup || !max_packet || len > UHCI_CTRL_DMA_SIZE)
        return false;
    if (!hc->setup_buffer || !hc->control_buffer) return false;

    in = (setup->type & USB_DIR_IN) != 0;
    uhci_encode_setup(hc->setup_buffer, setup);
    uhci_dma_fence();
    if (!uhci_validate_setup_dma(hc->setup_buffer, setup, "CONTROL"))
        return false;
    kmemset(hc->control_buffer, 0, UHCI_CTRL_DMA_SIZE);
    if (!in && len && data)
        kmemcpy(hc->control_buffer, data, len);

    /* Build the complete TD chain while the Queue Head is detached. */
    head = uhci_alloc_td(hc);
    if (!head) return false;
    uhci_td_init(head, NULL, UHCI_PID_SETUP, addr, 0, 0,
                 hc->setup_buffer, sizeof(usb_setup_t), low_speed);
    prev = head;

    pid = in ? UHCI_PID_IN : UHCI_PID_OUT;
    while (offset < len) {
        uint32_t chunk = len - offset;
        if (chunk > max_packet) chunk = max_packet;
        td = uhci_alloc_td(hc);
        if (!td) {
            uhci_free_chain(head);
            return false;
        }
        uhci_td_init(td, prev, pid, addr, 0, toggle,
                     hc->control_buffer + offset, chunk, low_speed);
        /* SPD belongs only on non-final IN packets. */
        if (in && offset + chunk < len)
            td->status |= UHCI_TD_STS_SPD;
        prev = td;
        toggle ^= 1U;
        offset += chunk;
    }

    td = uhci_alloc_td(hc);
    if (!td) {
        uhci_free_chain(head);
        return false;
    }
    uhci_td_init(td, prev,
                 in ? UHCI_PID_OUT : UHCI_PID_IN,
                 addr, 0, 1, NULL, 0, low_speed);
    td->status |= UHCI_TD_STS_IOC;

    /* The QH is still detached, so the complete chain can be published now. */
    uhci_dma_fence();

    if (!uhci_run_chain(hc, head, log_errors, "CONTROL",
                        UHCI_CONTROL_TIMEOUT_MS, NULL)) {
        if (log_errors) {
            kprintf("  [USB-DIAG] control request fallo type=%x req=%x value=%x index=%x len=%u\n",
                    setup->type, setup->req, setup->value,
                    setup->index, setup->len);
            kprintf("  [USB-DIAG] UHCI 1.1 queue=depth actlen=HC-writeback\n");
        }
        if (in && data && len &&
            uhci_control_split_in(hc, addr, max_packet, setup, data, len,
                                  low_speed))
            return true;
        return false;
    }

    uhci_dma_fence();
    if (in && len && data)
        kmemcpy(data, hc->control_buffer, len);
    return true;
}

/*
 * Execute exactly one SETUP transaction in the UHCI software-debug mode.
 * Keeping C_ERR at three preserves the result of the first attempt instead
 * of collapsing three timeouts into CRC/Timeout + Stalled.  This diagnostic
 * is used only after normal enumeration retries have failed.
 */
static void uhci_debug_setup_once(uhci_controller_t *hc, uint32_t port,
                                  uint8_t addr, const usb_setup_t *setup,
                                  bool low_speed) {
    uhci_td_t *td;
    uint16_t base_cmd = UHCI_CMD_CF | UHCI_CMD_MAXP;
    uint16_t cmd;
    uint16_t host_status;
    uint16_t port_before = 0;
    uint16_t port_after = 0;
    uint32_t status;
    uint32_t timeout;
    bool completed = false;

    if (!hc || !hc->ready || !setup || port >= hc->port_count) return;
    if (hc->swdbg_done_mask & (uint8_t)(1U << port)) return;
    td = uhci_alloc_td(hc);
    if (!td) return;
    hc->swdbg_done_mask |= (uint8_t)(1U << port);

    uhci_encode_setup(hc->setup_buffer, setup);
    uhci_dma_fence();
    if (!uhci_validate_setup_dma(hc->setup_buffer, setup, "SWDBG")) {
        uhci_free_chain(td);
        return;
    }
    uhci_td_init(td, NULL, UHCI_PID_SETUP, addr, 0, 0,
                 hc->setup_buffer, sizeof(*setup), low_speed);
    uhci_dma_fence();

    port_before = uhci_inw((uint16_t)(hc->io_base + UHCI_PORTSC0 +
                                      port * 2U));
    uhci_park_schedule(hc);
    uhci_dma_fence();

    /* SWDBG may only be changed while the controller is stopped. */
    uhci_outw((uint16_t)(hc->io_base + UHCI_USBCMD), base_cmd);
    if (!uhci_wait_halted(hc, true, 2000000U)) {
        kprintf("  [USB-SWDBG] no se pudo detener el HC\n");
        goto restore;
    }

    uhci_outw((uint16_t)(hc->io_base + UHCI_USBCMD),
              base_cmd | UHCI_CMD_SWDBG);
    cmd = uhci_inw((uint16_t)(hc->io_base + UHCI_USBCMD));
    if (!(cmd & UHCI_CMD_SWDBG)) {
        kprintf("  [USB-SWDBG] el HC no acepto SWDBG cmd=%x\n", cmd);
        goto restore;
    }

    uhci_outw((uint16_t)(hc->io_base + UHCI_USBSTS), UHCI_STS_CLEAR);
    hc->qh->head = uhci_terminator_link(hc);
    hc->qh->element = (uint32_t)(uintptr_t)td;
    uhci_dma_fence();
    uhci_outw((uint16_t)(hc->io_base + UHCI_USBCMD),
              base_cmd | UHCI_CMD_SWDBG | UHCI_CMD_RS);

    timeout = 4000000U;
    while (timeout) {
        cmd = uhci_inw((uint16_t)(hc->io_base + UHCI_USBCMD));
        host_status = uhci_inw((uint16_t)(hc->io_base + UHCI_USBSTS));
        if (!(cmd & UHCI_CMD_RS) && (host_status & UHCI_STS_HALTED)) {
            completed = true;
            break;
        }
        timeout--;
        __asm__ volatile ("pause");
    }

    uhci_dma_fence();
    status = td->status;
    port_after = uhci_inw((uint16_t)(hc->io_base + UHCI_PORTSC0 +
                                     port * 2U));
    kprintf("  [USB-SWDBG] one-step done=%u cmd=%x usbsts=%x port=%x->%x\n",
            completed, cmd,
            uhci_inw((uint16_t)(hc->io_base + UHCI_USBSTS)),
            port_before, port_after);
    kprintf("  [USB-SWDBG] td=%x sts=%x cerr=%u active=%u nak=%u crc=%u stalled=%u actraw=%x head=%x elem=%x\n",
            (uint32_t)(uintptr_t)td, status,
            (status & UHCI_TD_STS_ERRCNT_MASK) >> 27,
            (status & UHCI_TD_STS_ACTIVE) != 0,
            (status & UHCI_TD_STS_NAK) != 0,
            (status & UHCI_TD_STS_CRC_TIMEOUT) != 0,
            (status & UHCI_TD_STS_STALLED) != 0,
            status & UHCI_TD_ACTLEN_MASK,
            hc->qh->head,
            hc->qh->element);
    if (!completed)
        kprintf("  [USB-SWDBG] resultado=HC-no-completo\n");
    else if (status & UHCI_TD_STS_NAK)
        kprintf("  [USB-SWDBG] resultado=NAK-en-SETUP\n");
    else if ((status & UHCI_TD_STS_STALLED) &&
             ((status & UHCI_TD_STS_ERRCNT_MASK) != 0))
        kprintf("  [USB-SWDBG] resultado=STALL-en-SETUP\n");
    else if (status & (UHCI_TD_STS_CRC_TIMEOUT |
                       UHCI_TD_STS_BITSTUFF))
        kprintf("  [USB-SWDBG] resultado=timeout-CRC-senal\n");
    else if (!(status & UHCI_TD_STS_ACTIVE))
        kprintf("  [USB-SWDBG] resultado=ACK\n");
    else
        kprintf("  [USB-SWDBG] resultado=indeterminado\n");

restore:
    /* Stop first while preserving SWDBG; only a halted HC has released td. */
    uhci_park_schedule(hc);
    uhci_dma_fence();
    cmd = uhci_inw((uint16_t)(hc->io_base + UHCI_USBCMD));
    uhci_outw((uint16_t)(hc->io_base + UHCI_USBCMD),
              cmd & (uint16_t)~UHCI_CMD_RS);
    if (!uhci_wait_halted(hc, true, 2000000U)) {
        /* The PIIX4 may still own td; leaking one static TD is safer. */
        kprintf("  [USB-SWDBG] timeout deteniendo HC; TD no reciclado\n");
        hc->ready = false;
        return;
    }
    /* A final TD write-back may have restored QH.element while stopping. */
    uhci_park_schedule(hc);
    uhci_dma_fence();
    uhci_outw((uint16_t)(hc->io_base + UHCI_USBCMD), base_cmd);
    (void)uhci_inw((uint16_t)(hc->io_base + UHCI_USBCMD));
    uhci_free_chain(td);
    uhci_outw((uint16_t)(hc->io_base + UHCI_USBSTS), UHCI_STS_CLEAR);
    uhci_outw((uint16_t)(hc->io_base + UHCI_USBCMD), UHCI_CMD_RUN);
    (void)uhci_wait_halted(hc, false, 2000000U);
}

static bool usb_request(uhci_controller_t *hc, uint8_t addr,
                        uint8_t max_packet, bool low_speed, uint8_t type,
                        uint8_t req, uint16_t value, uint16_t index,
                        uint16_t len, void *data) {
    usb_setup_t setup;
    setup.type = type;
    setup.req = req;
    setup.value = value;
    setup.index = index;
    setup.len = len;
    return uhci_control(hc, addr, max_packet, &setup, data, len,
                        low_speed, true);
}

/* GET_MAX_LUN is allowed to STALL on a single-LUN BOT device.  Keep that
   standards-compliant response out of the generic fatal-looking dump. */
static bool usb_request_quiet(uhci_controller_t *hc, uint8_t addr,
                              uint8_t max_packet, bool low_speed,
                              uint8_t type, uint8_t req, uint16_t value,
                              uint16_t index, uint16_t len, void *data) {
    usb_setup_t setup;
    setup.type = type;
    setup.req = req;
    setup.value = value;
    setup.index = index;
    setup.len = len;
    return uhci_control(hc, addr, max_packet, &setup, data, len,
                        low_speed, false);
}

static bool uhci_bulk_batch(uhci_mass_device_t *dev, bool in,
                            uint8_t *data, uint32_t len,
                            uint32_t *packets_done,
                            uint32_t *bytes_done, bool *short_packet,
                            const char *stage, uint32_t timeout_ms) {
    uhci_controller_t *hc = dev->hc;
    uint8_t endpoint = in ? (dev->bulk_in & 0x0F) :
                            (dev->bulk_out & 0x0F);
    uint16_t max_packet = in ? dev->bulk_in_packet : dev->bulk_out_packet;
    uint8_t initial_toggle = in ? dev->toggle_in : dev->toggle_out;
    uint8_t toggle = initial_toggle;
    uint8_t pid = in ? UHCI_PID_IN : UHCI_PID_OUT;
    uhci_td_t *head = NULL;
    uhci_td_t *prev = NULL;
    uint32_t packets = 0;
    uhci_chain_result_t result;

    if (packets_done) *packets_done = 0;
    if (bytes_done) *bytes_done = 0;
    if (short_packet) *short_packet = false;
    if (!max_packet || max_packet > 64U) return false;

    while (len && packets < UHCI_MAX_TD) {
        uint32_t chunk = len > max_packet ? max_packet : len;
        uhci_td_t *td = uhci_alloc_td(hc);
        if (!td) {
            if (head) uhci_free_chain(head);
            return false;
        }
        if (!head) head = td;
        uhci_td_init(td, prev, pid, dev->addr, endpoint, toggle,
                     data, chunk, false);
        /* Linux UHCI clears SPD on the last bulk TD.  Some PIIX4-era
           controllers misreport an otherwise complete short-than-MPS IN
           packet as fatal when SPD is set on the final descriptor. */
        if (in && len > chunk)
            td->status |= UHCI_TD_STS_SPD;
        prev = td;
        toggle ^= 1U;
        data += chunk;
        len -= chunk;
        packets++;
    }

    if (!in && len == 0 && packets == 1U &&
        g_uhci_bot_trace_budget && head) {
        kprintf("  [USB-BOT] sizeof(CBW)=%u tx_bytes=%u td_exp=%u\n",
                sizeof(usb_msc_cbw_t), MSC_CBW_WIRE_SIZE,
                uhci_td_expected_length(head->token));
    }

    if (!head)
        return false;
    if (!uhci_run_chain(hc, head, true,
                        stage ? stage : (in ? "BULK-IN" : "BULK-OUT"),
                        timeout_ms ? timeout_ms : UHCI_BULK_TIMEOUT_MS,
                        &result)) {
        if (packets_done) *packets_done = result.completed_tds;
        if (bytes_done) *bytes_done = result.actual_bytes;
        if (short_packet) *short_packet = result.short_packet;
        if (result.failed_valid) {
            if (in) dev->toggle_in = result.failed_toggle;
            else dev->toggle_out = result.failed_toggle;
            kprintf("  [USB-DIAG] bulk fail %s ep=%u pid=%x tog=%u exp=%u sts=%x bytes=%u\n",
                    stage ? stage : (in ? "BULK-IN" : "BULK-OUT"),
                    result.failed_endpoint, result.failed_pid,
                    result.failed_toggle,
                    uhci_td_expected_length(result.failed_token),
                    result.failed_status, result.actual_bytes);
        }
        return false;
    }
    if (!result.completed_tds || result.completed_tds > packets)
        return false;
    if (packets_done) *packets_done = result.completed_tds;
    if (bytes_done) *bytes_done = result.actual_bytes;
    if (short_packet) *short_packet = result.short_packet;
    toggle = initial_toggle ^ (uint8_t)(result.completed_tds & 1U);
    if (in) dev->toggle_in = toggle;
    else dev->toggle_out = toggle;
    return true;
}

static void usb_hex_line(char *dst, const uint8_t *src, uint32_t count) {
    static const char hex[] = "0123456789ABCDEF";
    uint32_t pos = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (i) dst[pos++] = ' ';
        dst[pos++] = hex[src[i] >> 4];
        dst[pos++] = hex[src[i] & 0x0FU];
    }
    dst[pos] = '\0';
}

static void usb_dump_hex(const char *label, const uint8_t *src,
                         uint32_t count) {
    char line[48];
    uint32_t off = 0;

    while (off < count) {
        uint32_t chunk = count - off;
        if (chunk > 16U) chunk = 16U;
        usb_hex_line(line, src + off, chunk);
        kprintf("  [USB-DIAG] %s[%u]: %s\n", label, off, line);
        off += chunk;
    }
}

static void usb_msc_trace_cbw_dma(const uhci_mass_device_t *dev,
                                  const uint8_t *b) {
    char line[48];
    if (!dev || !b || !g_uhci_bot_trace_budget) return;
    if (b[0] != 0x55U || b[1] != 0x53U ||
        b[2] != 0x42U || b[3] != 0x43U)
        return;

    kprintf("  [USB-BOT] CBW-DMA buf=%x out-tog=%u in-tog=%u\n",
            (uint32_t)(uintptr_t)b, dev->toggle_out, dev->toggle_in);
    usb_hex_line(line, b, 4);
    kprintf("  [USB-BOT] CBW[00..03] sig: %s\n", line);
    usb_hex_line(line, b + 4, 4);
    kprintf("  [USB-BOT] CBW[04..07] tag: %s\n", line);
    usb_hex_line(line, b + 8, 4);
    kprintf("  [USB-BOT] CBW[08..11] len: %s\n", line);
    usb_hex_line(line, b + 12, 3);
    kprintf("  [USB-BOT] CBW[12..14] flags/lun/cb_len: %s\n", line);
    usb_hex_line(line, b + 15, 8);
    kprintf("  [USB-BOT] CBWCB[00..07]: %s\n", line);
    usb_hex_line(line, b + 23, 8);
    kprintf("  [USB-BOT] CBWCB[08..15]: %s\n", line);
    g_uhci_bot_trace_budget--;
}

static bool uhci_bulk(uhci_mass_device_t *dev, bool in, void *data,
                      uint32_t len, const char *stage,
                      uint32_t timeout_ms, uint32_t *actual_len) {
    uhci_controller_t *hc;
    uint16_t max_packet;
    uint8_t *cursor = (uint8_t *)data;
    uint32_t transferred = 0;

    if (actual_len) *actual_len = 0;
    if (!dev || !dev->hc) return false;
    if (len == 0) return true;
    if (!data) return false;
    hc = dev->hc;
    if (!hc->control_buffer) return false;
    max_packet = in ? dev->bulk_in_packet : dev->bulk_out_packet;
    if (!max_packet || max_packet > 64U) return false;

    /* Never expose a temporary stack/heap buffer directly to the PIIX4.
       Reuse the aligned, identity-mapped controller DMA arena in bounded
       batches, then copy between it and the caller's buffer. */
    while (len) {
        uint32_t max_batch = (uint32_t)max_packet * UHCI_MAX_TD;
        uint32_t batch;
        uint32_t packets = 0;
        uint32_t bytes = 0;
        bool short_packet = false;

        if (max_batch > UHCI_CTRL_DMA_SIZE)
            max_batch = UHCI_CTRL_DMA_SIZE;
        batch = len > max_batch ? max_batch : len;
        if (!in)
            kmemcpy(hc->control_buffer, cursor, batch);
        else
            kmemset(hc->control_buffer, 0, batch);
        if (!in && batch == MSC_CBW_WIRE_SIZE)
            usb_msc_trace_cbw_dma(dev, hc->control_buffer);
        uhci_dma_fence();

        if (!uhci_bulk_batch(dev, in, hc->control_buffer, batch,
                             &packets, &bytes, &short_packet,
                             stage, timeout_ms))
            return false;
        if (!packets || bytes > batch) return false;

        uhci_dma_fence();
        if (in) {
            if (bytes) kmemcpy(cursor, hc->control_buffer, bytes);
            transferred += bytes;
            if (actual_len) *actual_len = transferred;
            if (short_packet) return true;
            if (bytes != batch) return false;
        } else {
            if (bytes != batch || short_packet) return false;
            transferred += batch;
            if (actual_len) *actual_len = transferred;
        }
        cursor += batch;
        len -= batch;
    }
    return true;
}

static bool usb_clear_halt(uhci_mass_device_t *dev, uint8_t endpoint) {
    bool ok;
    if (!dev || !dev->hc) return false;
    ok = usb_request(dev->hc, dev->addr, dev->max_packet0, false,
                     RT_HOST_TO_DEV | RT_STANDARD | RT_ENDPOINT,
                     REQ_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_HALT,
                     endpoint, 0, NULL);
    if (ok) {
        if (endpoint & USB_DIR_IN)
            dev->toggle_in = 0;
        else
            dev->toggle_out = 0;
    }
    return ok;
}

static bool usb_msc_get_max_lun(uhci_mass_device_t *dev) {
    uint8_t max_lun = 0xFFU;
    bool ok;

    if (!dev || !dev->hc) return false;
    ok = usb_request_quiet(dev->hc, dev->addr, dev->max_packet0, false,
                           RT_DEV_TO_HOST | RT_CLASS | RT_INTERFACE,
                           MSC_REQ_GET_MAX_LUN, 0,
                           dev->interface_number, 1, &max_lun);
    if (!ok) {
        /* BOT 1.0 explicitly allows a single-LUN device to STALL this
           request.  A following SETUP (the initial BOT reset below)
           recovers endpoint zero; LUN 0 remains the only legal target. */
        dev->max_lun = 0;
        kprintf("  [USB-BOT] GET_MAX_LUN STALL/no soportado; usa LUN 0\n");
        return true;
    }
    if (max_lun > 15U) {
        kprintf("  [USB-BOT] GET_MAX_LUN invalido=%u\n", max_lun);
        return false;
    }
    dev->max_lun = max_lun;
    kprintf("  [USB-BOT] GET_MAX_LUN=%u\n", max_lun);
    return true;
}

static bool usb_msc_reset_recovery(uhci_mass_device_t *dev) {
    bool ok;
    if (!dev || !dev->hc) return false;

    ok = usb_request(dev->hc, dev->addr, dev->max_packet0, false,
                     RT_HOST_TO_DEV | RT_CLASS | RT_INTERFACE,
                     MSC_REQ_RESET, 0, dev->interface_number, 0, NULL);
    kprintf("  [USB-BOT] reset settle %u ms antes de CLEAR_FEATURE\n",
            MSC_RESET_PRE_CLEAR_MS);
    usb_delay_ms(MSC_RESET_PRE_CLEAR_MS);
    ok = usb_clear_halt(dev, dev->bulk_in) && ok;
    ok = usb_clear_halt(dev, dev->bulk_out) && ok;
    uhci_outw((uint16_t)(dev->hc->io_base + UHCI_USBSTS),
              UHCI_STS_CLEAR);
    usb_delay_ms(MSC_RESET_POST_CLEAR_MS);
    kprintf("  [USB-BOT] reset-recovery=%s toggles IN=0 OUT=0\n",
            ok ? "ok" : "fallo");
    return ok;
}

static bool usb_msc_receive_csw(uhci_mass_device_t *dev,
                                usb_msc_csw_t *csw) {
    uint32_t actual = 0;

    if (!dev || !csw) return false;
    kmemset(csw, 0, sizeof(*csw));
    if (uhci_bulk(dev, true, csw, sizeof(*csw), "MSC-CSW-IN",
                  UHCI_CSW_TIMEOUT_MS, &actual)) {
        if (actual == sizeof(*csw)) return true;
        kprintf("  [USB-BOT] CSW truncado actual=%u esperado=%u\n",
                actual, sizeof(*csw));
        return false;
    }

    /* BOT 1.0, figure 2: after a STALL or bulk error while reading the
       CSW, clear Bulk-IN HALT and attempt that same CSW exactly once. */
    kprintf("  [USB-BOT] CSW no llego; CLEAR_FEATURE(Bulk-IN) y reintento\n");
    if (!usb_clear_halt(dev, dev->bulk_in)) return false;
    kmemset(csw, 0, sizeof(*csw));
    actual = 0;
    if (!uhci_bulk(dev, true, csw, sizeof(*csw), "MSC-CSW-RETRY",
                   UHCI_CSW_TIMEOUT_MS, &actual))
        return false;
    if (actual != sizeof(*csw)) {
        kprintf("  [USB-BOT] CSW retry truncado actual=%u esperado=%u\n",
                actual, sizeof(*csw));
        return false;
    }
    return true;
}

static bool usb_msc_command(uhci_mass_device_t *dev, const uint8_t *cmd,
                            uint8_t cmd_len, bool in, void *data,
                            uint32_t data_len) {
    uint8_t cbw[MSC_CBW_WIRE_SIZE];
    usb_msc_csw_t csw;
    uint32_t tag;
    uint32_t actual = 0;
    uint32_t data_actual = 0;
    bool data_stage_error = false;
    bool trace;

    if (!dev || !cmd || cmd_len == 0 || cmd_len > 16) return false;
    trace = g_uhci_bot_trace_budget != 0U;
    dev->last_csw_status = 0xFFU;
    kmemset(cbw, 0, sizeof(cbw));
    tag = ++dev->tag;
    wr32le(cbw + 0, CBW_SIGNATURE);
    wr32le(cbw + 4, tag);
    wr32le(cbw + 8, data_len);
    /* ReactOS' usbstor marks every non-WRITE CDB as IN, even when the
       transfer length is zero.  BOT says the bit is ignored in that case,
       but using the mature-driver convention makes TUR diagnostics match. */
    cbw[12] = in ? USB_DIR_IN : 0;
    cbw[13] = 0;
    cbw[14] = cmd_len;
    kmemcpy(cbw + 15, cmd, cmd_len);

    if (trace)
        kprintf("  [USB-BOT] op=%x tag=%x data=%u dir=%s CBW-tog=%u CSW-tog=%u\n",
                cmd[0], tag, data_len,
                data_len ? (in ? "IN" : "OUT") : "NONE",
                dev->toggle_out, dev->toggle_in);
    if (!uhci_bulk(dev, false, cbw, sizeof(cbw), "MSC-CBW-OUT",
                   UHCI_CBW_TIMEOUT_MS, &actual) ||
        actual != sizeof(cbw))
        return false;
    if (trace)
        kprintf("  [USB-BOT] CBW ACK tag=%x next-OUT-tog=%u\n",
                tag, dev->toggle_out);
    if (data_len) {
        if (trace)
            kprintf("  [USB-BOT] post-CBW delay %u ms\n",
                    MSC_POST_CBW_DELAY_MS);
        usb_delay_ms(MSC_POST_CBW_DELAY_MS);
    }
    if (data_len && !uhci_bulk(dev, in, data, data_len,
                               in ? "MSC-DATA-IN" : "MSC-DATA-OUT",
                               UHCI_BULK_TIMEOUT_MS, &data_actual)) {
        if (in && data_actual == 0U) {
            dev->last_csw_status = 0xFDU;
            kprintf("  [USB-BOT] DATA IN NAK/timeout sin bytes; reset recovery directo\n");
            return false;
        }
        /* A DATA STALL is a command-level outcome in BOT, not yet proof
           that the transport is lost.  Clear that pipe and still request
           the command's CSW; its status/residue decides what follows. */
        uint8_t endpoint = in ? dev->bulk_in : dev->bulk_out;
        data_stage_error = true;
        kprintf("  [USB-BOT] DATA %s fallo; clear-halt y lee CSW\n",
                in ? "IN" : "OUT");
        if (!usb_clear_halt(dev, endpoint)) return false;
    }
    if (!usb_msc_receive_csw(dev, &csw))
        return false;
    if (csw.signature != CSW_SIGNATURE || csw.tag != tag ||
        csw.residue > data_len || csw.status > 2U) {
        if (g_uhci_fail_log_budget) {
            kprintf("  [USB-DIAG] CSW invalido sig=%x tag=%x/%x residue=%u status=%u\n",
                    csw.signature, csw.tag, tag,
                    csw.residue, csw.status);
            g_uhci_fail_log_budget--;
        }
        return false;
    }
    dev->last_csw_status = csw.status;
    if (trace)
        kprintf("  [USB-BOT] CSW tag=%x status=%u residue=%u next-IN-tog=%u\n",
                csw.tag, csw.status, csw.residue, dev->toggle_in);
    if (csw.status == 2U) {
        kprintf("  [USB-BOT] phase error: requiere reset-recovery\n");
        return false;
    }
    if (csw.status == 1U) return false;
    if (data_stage_error) {
        dev->last_csw_status = 0xFEU;
        return false;
    }
    if (csw.residue != 0U && cmd[0] != SCSI_INQUIRY &&
        cmd[0] != SCSI_REQUEST_SENSE) {
        /* Our block-I/O commands require exact-length data.  Do not expose
           a partially filled sector as a successful read. */
        dev->last_csw_status = 0xFEU;
        return false;
    }
    if (data_len && data_actual != data_len &&
        cmd[0] != SCSI_INQUIRY && cmd[0] != SCSI_REQUEST_SENSE) {
        dev->last_csw_status = 0xFEU;
        kprintf("  [USB-BOT] DATA corto op=%x actual=%u esperado=%u\n",
                cmd[0], data_actual, data_len);
        return false;
    }
    if (data_len && data_actual == 0U) {
        dev->last_csw_status = 0xFEU;
        return false;
    }
    return true;
}

static bool usb_msc_command_retry(uhci_mass_device_t *dev,
                                  const uint8_t *cmd, uint8_t cmd_len,
                                  bool in, void *data, uint32_t data_len) {
    if (usb_msc_command(dev, cmd, cmd_len, in, data, data_len)) return true;
    /* Command Failed is valid BOT transport.  Let the SCSI layer issue
       REQUEST SENSE; a transport reset would discard useful sense state. */
    if (dev->last_csw_status == 1U) return false;
    if (!usb_msc_reset_recovery(dev)) return false;
    if (usb_msc_command(dev, cmd, cmd_len, in, data, data_len)) return true;

    if (g_uhci_fail_log_budget) {
        kprintf("  [USB] UHCI MSC command failed op=%x len=%u\n",
                cmd_len ? cmd[0] : 0, data_len);
        g_uhci_fail_log_budget--;
    }
    return false;
}

static bool usb_msc_request_sense(uhci_mass_device_t *dev) {
    uint8_t cmd[6];
    uint8_t data[18];

    kmemset(cmd, 0, sizeof(cmd));
    kmemset(data, 0, sizeof(data));
    cmd[0] = SCSI_REQUEST_SENSE;
    cmd[4] = sizeof(data);
    if (!usb_msc_command_retry(dev, cmd, sizeof(cmd), true,
                               data, sizeof(data)))
        return false;
    kprintf("  [USB-SCSI] sense response=%x key=%x asc=%x ascq=%x\n",
            data[0] & 0x7FU, data[2] & 0x0FU, data[12], data[13]);
    return true;
}

static bool usb_msc_inquiry(uhci_mass_device_t *dev) {
    uint8_t cmd[6];
    uint8_t data[36];

    kmemset(cmd, 0, sizeof(cmd));
    kmemset(data, 0, sizeof(data));
    cmd[0] = SCSI_INQUIRY;
    cmd[4] = sizeof(data);
    if (!usb_msc_command_retry(dev, cmd, sizeof(cmd), true,
                               data, sizeof(data))) {
        if (dev->last_csw_status == 1U)
            (void)usb_msc_request_sense(dev);
        return false;
    }
    kprintf("  [USB-SCSI] inquiry type=%u removable=%u version=%x vendor=%x%x%x%x product=%x%x%x%x\n",
            data[0] & 0x1FU, (data[1] >> 7) & 1U, data[2],
            data[8], data[9], data[10], data[11],
            data[16], data[17], data[18], data[19]);
    return true;
}

static bool usb_msc_test_unit_ready_once(uhci_mass_device_t *dev) {
    uint8_t cmd[6];

    kmemset(cmd, 0, sizeof(cmd));
    cmd[0] = SCSI_TEST_UNIT_READY;
    return usb_msc_command_retry(dev, cmd, sizeof(cmd), true, NULL, 0);
}

static bool usb_msc_wait_ready(uhci_mass_device_t *dev) {
    for (uint32_t retry = 0; retry < 5; retry++) {
        if (usb_msc_test_unit_ready_once(dev))
            return true;
        if (dev->last_csw_status == 1U)
            (void)usb_msc_request_sense(dev);
        else
            return false;
        usb_delay_ms(100U * (retry + 1U));
    }
    return false;
}

static bool usb_msc_read_capacity(uhci_mass_device_t *dev) {
    uint8_t cmd[10];
    uint8_t data[8];
    bool inquiry_ok;
    bool tur_ok;

    /*
     * Start with a no-data command.  ReactOS builds TEST UNIT READY as a
     * normal 6-byte BOT command with zero transfer length, so this tells us
     * whether CBW->CSW works independently from the DATA-IN TD path.
     */
    tur_ok = usb_msc_test_unit_ready_once(dev);
    kprintf("  [USB-SCSI] TUR inicial %s\n", tur_ok ? "OK" : "fallo");
    if (!tur_ok && dev->last_csw_status == 1U)
        (void)usb_msc_request_sense(dev);

    inquiry_ok = usb_msc_inquiry(dev);
    if (!inquiry_ok)
        kprintf("  [USB-SCSI] INQUIRY fallo; prueba TUR/capacity igualmente\n");
    if (!tur_ok && !usb_msc_wait_ready(dev))
        kprintf("  [USB-SCSI] unidad aun no ready; prueba READ CAPACITY\n");
    if (!inquiry_ok) {
        usb_delay_ms(250);
        if (usb_msc_inquiry(dev))
            kprintf("  [USB-SCSI] INQUIRY recuperado tras TUR\n");
    }
    kmemset(cmd, 0, sizeof(cmd));
    cmd[0] = SCSI_READ_CAPACITY10;
    for (uint32_t retry = 0; retry < 4; retry++) {
        kmemset(data, 0, sizeof(data));
        if (usb_msc_command_retry(dev, cmd, sizeof(cmd), true,
                                  data, sizeof(data))) {
            dev->sectors = rd32be(data) + 1U;
            return rd32be(data + 4) == BLOCK_SECTOR_SIZE &&
                   dev->sectors != 0;
        }
        if (dev->last_csw_status == 1U)
            (void)usb_msc_request_sense(dev);
        usb_delay_ms(100U * (retry + 1U));
    }
    return false;
}

static bool usb_msc_rw(uhci_mass_device_t *dev, uint32_t lba,
                       uint8_t count, void *buffer, bool write) {
    uint8_t cmd[10];
    if (!count || !buffer) return false;
    kmemset(cmd, 0, sizeof(cmd));
    cmd[0] = write ? SCSI_WRITE10 : SCSI_READ10;
    wr32be(cmd + 2, lba);
    cmd[7] = 0;
    cmd[8] = count;
    return usb_msc_command_retry(dev, cmd, sizeof(cmd), !write, buffer,
                                 (uint32_t)count * BLOCK_SECTOR_SIZE);
}

static bool usb_block_read(block_device_t *block, uint32_t lba,
                           uint8_t count, void *buffer) {
    uhci_mass_device_t *dev = (uhci_mass_device_t *)block->driver_data;
    return dev && usb_msc_rw(dev, lba, count, buffer, false);
}

static bool usb_block_write(block_device_t *block, uint32_t lba,
                            uint8_t count, const void *buffer) {
    uhci_mass_device_t *dev = (uhci_mass_device_t *)block->driver_data;
    return dev && usb_msc_rw(dev, lba, count, (void *)buffer, true);
}

static bool usb_choose_name(char name[8]) {
    for (uint32_t index = 0; index < 10; index++) {
        name[0] = 'u';
        name[1] = 's';
        name[2] = 'b';
        name[3] = (char)('0' + index);
        name[4] = '\0';
        if (!block_get(name)) return true;
    }
    return false;
}

/* A low-speed keyboard or mouse cannot be mass storage, but its EP0 is a
   valuable independent test of the PIIX4 transmitter/receiver and reset. */
static void usb_probe_low_speed_ep0(uhci_controller_t *hc, uint32_t port) {
    uint8_t desc[8];
    usb_setup_t setup;
    uint8_t bit;

    if (!hc || port >= hc->port_count) return;
    bit = (uint8_t)(1U << port);
    if (hc->low_speed_probe_mask & bit) return;
    hc->low_speed_probe_mask |= bit;

    kmemset(desc, 0, sizeof(desc));
    if (usb_request(hc, 0, 8, true,
                    RT_DEV_TO_HOST | RT_STANDARD | RT_DEVICE,
                    REQ_GET_DESC, DESC_DEVICE << 8, 0, sizeof(desc), desc)) {
        kprintf("  [USB-DIAG] low-speed EP0 ACK desc=%x %x %x %x %x %x %x %x\n",
                desc[0], desc[1], desc[2], desc[3],
                desc[4], desc[5], desc[6], desc[7]);
        return;
    }

    kprintf("  [USB-DIAG] low-speed EP0 GET_DESCRIPTOR fallo\n");
    setup.type = RT_DEV_TO_HOST | RT_STANDARD | RT_DEVICE;
    setup.req = REQ_GET_DESC;
    setup.value = DESC_DEVICE << 8;
    setup.index = 0;
    setup.len = sizeof(desc);
    if (uhci_reset_port(hc, port)) {
        uhci_debug_setup_once(hc, port, 0, &setup, true);
        (void)uhci_reset_port(hc, port);
    }
}

static bool usb_try_mass_storage(uhci_controller_t *hc,
                                 uhci_mass_device_t *dev,
                                 uint32_t port) {
    usb_device_desc_t dd;
    uint8_t cfg[256];
    usb_config_desc_t *cd = (usb_config_desc_t *)cfg;
    usb_interface_desc_t *picked_intf = NULL;
    bool inside_picked = false;
    uint8_t addr = 1;
    uint8_t config_value;
    uint8_t active_config;
    uint16_t config_total;
    uint8_t bulk_in = 0;
    uint8_t bulk_out = 0;
    uint16_t bulk_in_packet = 0;
    uint16_t bulk_out_packet = 0;
    uint8_t *cursor;
    uint8_t *end;
    char hexline[16];
    bool got_descriptor = false;

    if (!hc || !dev || dev->registered) return false;

    uhci_log_port_state(hc, "pre-desc8");
    {
        uint16_t status = uhci_inw(uhci_port_addr(hc, port));
        if ((status & (UHCI_PORT_CONNECT | UHCI_PORT_ENABLE)) !=
            (UHCI_PORT_CONNECT | UHCI_PORT_ENABLE)) {
            kprintf("  [USB] UHCI puerto %u no listo para address0 portsc=%x\n",
                    port, status);
            return false;
        }
    }
    usb_delay_ms(100);

    kmemset(&dd, 0, sizeof(dd));
    for (uint32_t attempt = 0; attempt < 3; attempt++) {
        uint16_t fr0 = uhci_inw((uint16_t)(hc->io_base + UHCI_FRNUM));
        bool request_ok = usb_request(
            hc, 0, 8, false,
            RT_DEV_TO_HOST | RT_STANDARD | RT_DEVICE,
            REQ_GET_DESC, (DESC_DEVICE << 8), 0, 8, &dd);
        uint16_t fr1 = uhci_inw((uint16_t)(hc->io_base + UHCI_FRNUM));
        uint16_t portsc = uhci_inw(uhci_port_addr(hc, port));

        kprintf("  [USB-EP0] desc8 try=%u ok=%u fr=%u->%u delta=%u portsc=%x cmd=%x sts=%x\n",
                attempt + 1U, request_ok, fr0, fr1,
                (fr1 - fr0) & 0x07FFU, portsc,
                uhci_inw((uint16_t)(hc->io_base + UHCI_USBCMD)),
                uhci_inw((uint16_t)(hc->io_base + UHCI_USBSTS)));
        if (request_ok) {
            got_descriptor = true;
            break;
        }
        kprintf("  [USB] UHCI GET_DESCRIPTOR(8) intento %u fallo\n",
                attempt + 1U);
        if (attempt < 2) {
            /* A USB reset is the only standards-defined way to recover a
               device whose inherited BIOS address/state is unknown. */
            if (!uhci_reset_port(hc, port)) break;
            kmemset(&dd, 0, sizeof(dd));
        }
    }

    if (!got_descriptor) {
        usb_setup_t diag_setup;
        uint8_t inherited_addr = 0;
        uint8_t port_bit = (uint8_t)(1U << port);
        diag_setup.type = RT_DEV_TO_HOST | RT_STANDARD | RT_DEVICE;
        diag_setup.req = REQ_GET_DESC;
        diag_setup.value = DESC_DEVICE << 8;
        diag_setup.index = 0;
        diag_setup.len = 8;

        if (!(hc->swdbg_done_mask & port_bit)) {
            bool direct_ack =
                uhci_debug_setup_direct_frame(hc, &diag_setup, false);
            kprintf("  [USB-EP0] prueba sin QH resultado=%s\n",
                    direct_ack ? "ACK" : "CRC/timeout");
            /* ACK leaves EP0 waiting for DATA; failure may leave retries.
               Reset in both cases before any subsequent diagnostic. */
            if (!hc->ready || !uhci_reset_port(hc, port)) return false;
        }

        /* If a real bus reset happened, no USB device may still answer at
           its BIOS-assigned address. Scan once to prove that invariant. */
        if (!(hc->swdbg_done_mask & port_bit)) {
            uint32_t saved_trace_budget = g_uhci_td_trace_budget;
            g_uhci_td_trace_budget = 0;
            for (uint16_t candidate = 1; candidate <= 127; candidate++) {
                if (uhci_probe_setup_address_quiet(
                        hc, (uint8_t)candidate, &diag_setup, false)) {
                    inherited_addr = (uint8_t)candidate;
                    break;
                }
            }
            g_uhci_td_trace_budget = saved_trace_budget;
            if (inherited_addr)
                kprintf("  [USB-EP0] ALERTA address BIOS %u aun responde tras PORT RESET\n",
                        inherited_addr);
            else
                kprintf("  [USB-EP0] address heredada 1..127: ninguna responde\n");
        }

        /* Capture one uncollapsed transaction, then leave EP0 in Default. */
        if (uhci_reset_port(hc, port)) {
            uhci_debug_setup_once(hc, port, 0, &diag_setup, false);
            if (uhci_reset_port(hc, port) &&
                usb_request(hc, 0, 8, false,
                            RT_DEV_TO_HOST | RT_STANDARD | RT_DEVICE,
                            REQ_GET_DESC, (DESC_DEVICE << 8), 0, 8, &dd)) {
                got_descriptor = true;
                kprintf("  [USB] UHCI GET_DESCRIPTOR(8) recuperado tras SWDBG\n");
            }
        }
        if (!got_descriptor) return false;
    }
    kprintf("  [USB-DIAG] descriptor8 len=%u type=%u usb=%x class=%x maxp0=%u\n",
            dd.len, dd.type, dd.usb, dd.dev_class, dd.max_packet0);
    if (dd.max_packet0 != 8 && dd.max_packet0 != 16 &&
        dd.max_packet0 != 32 && dd.max_packet0 != 64)
        dd.max_packet0 = 8;

    if (!usb_request(hc, 0, dd.max_packet0, false,
                     RT_HOST_TO_DEV | RT_STANDARD | RT_DEVICE,
                     REQ_SET_ADDR, addr, 0, 0, NULL))
        return false;
    usb_delay_ms(5);

    if (!usb_request(hc, addr, dd.max_packet0, false,
                     RT_DEV_TO_HOST | RT_STANDARD | RT_DEVICE,
                     REQ_GET_DESC, (DESC_DEVICE << 8), 0,
                     sizeof(dd), &dd))
        return false;
    usb_dump_hex("device-desc", (const uint8_t *)&dd, sizeof(dd));
    kprintf("  [USB-DIAG] device addr=%u vid=%x pid=%x usb=%x configs=%u maxp0=%u\n",
            addr, dd.vendor, dd.product, dd.usb, dd.configs,
            dd.max_packet0);
    kprintf("  [USB-DIAG] device raw vid=%x pid=%x configs=%u\n",
            rd16(((const uint8_t *)&dd) + 8),
            rd16(((const uint8_t *)&dd) + 10),
            ((const uint8_t *)&dd)[17]);

    kmemset(cfg, 0, sizeof(cfg));
    if (!usb_request(hc, addr, dd.max_packet0, false,
                     RT_DEV_TO_HOST | RT_STANDARD | RT_DEVICE,
                     REQ_GET_DESC, (DESC_CONFIG << 8), 0,
                     sizeof(*cd), cfg))
        return false;
    config_total = rd16(cfg + 2);
    usb_dump_hex("config-head", cfg, sizeof(*cd));
    kprintf("  [USB-DIAG] config header len=%u type=%u total=%u intf=%u value=%u\n",
            cd->len, cfg[1], config_total,
            cd->interfaces, cd->config_value);
    if (config_total < sizeof(*cd) || config_total > sizeof(cfg))
        return false;
    if (!usb_request(hc, addr, dd.max_packet0, false,
                     RT_DEV_TO_HOST | RT_STANDARD | RT_DEVICE,
                     REQ_GET_DESC, (DESC_CONFIG << 8), 0,
                     config_total, cfg))
        return false;
    usb_dump_hex("config-desc", cfg, config_total);
    kprintf("  [USB-DIAG] config full len=%u total=%u intf=%u value=%u attrs=%x maxpwr=%u\n",
            cd->len, rd16(cfg + 2), cd->interfaces,
            cd->config_value, cd->attributes, cd->max_power);

    config_value = cd->config_value;
    if (config_value == 0U) {
        kprintf("  [USB-DIAG] config value 0 invalido para SET_CONFIGURATION\n");
        return false;
    }
    cursor = cfg + cd->len;
    end = cfg + config_total;
    while (cursor + 2 <= end && cursor[0]) {
        if (cursor + cursor[0] > end) break;
        if (cursor[1] == DESC_INTERFACE) {
            usb_interface_desc_t *id = (usb_interface_desc_t *)cursor;
            inside_picked = false;
            if (!picked_intf &&
                id->intf_class == USB_CLASS_MASS_STORAGE &&
                id->intf_subclass == USB_SUBCLASS_SCSI &&
                id->intf_protocol == USB_PROTOCOL_BULK_ONLY) {
                picked_intf = id;
                inside_picked = true;
                bulk_in = bulk_out = 0;
                bulk_in_packet = bulk_out_packet = 0;
            }
        } else if (inside_picked && cursor[1] == DESC_ENDPOINT) {
            usb_endpoint_desc_t *ed = (usb_endpoint_desc_t *)cursor;
            if ((ed->attributes & 0x03) == USB_EP_ATTR_BULK) {
                uint16_t packet = rd16(&ed->max_packet) & 0x07FFU;
                if (ed->addr & USB_DIR_IN) {
                    bulk_in = ed->addr;
                    bulk_in_packet = packet;
                } else {
                    bulk_out = ed->addr;
                    bulk_out_packet = packet;
                }
            }
        }
        cursor += cursor[0];
    }

    kprintf("  [USB-DIAG] MSC intf=%u alt=%u class=%x subclass=%x proto=%x bulk-in=%x/%u bulk-out=%x/%u\n",
            picked_intf ? picked_intf->number : 0xFFU,
            picked_intf ? picked_intf->alternate : 0xFFU,
            picked_intf ? picked_intf->intf_class : 0xFFU,
            picked_intf ? picked_intf->intf_subclass : 0xFFU,
            picked_intf ? picked_intf->intf_protocol : 0xFFU,
            bulk_in, bulk_in_packet, bulk_out, bulk_out_packet);
    kprintf("  [USB-DIAG] Interface Number=%u Alternate Setting=%u\n",
            picked_intf ? picked_intf->number : 0xFFU,
            picked_intf ? picked_intf->alternate : 0xFFU);
    {
        uint8_t intf_hex[3];
        uint8_t ep_hex[2];
        intf_hex[0] = picked_intf ? picked_intf->intf_class : 0xFFU;
        intf_hex[1] = picked_intf ? picked_intf->intf_subclass : 0xFFU;
        intf_hex[2] = picked_intf ? picked_intf->intf_protocol : 0xFFU;
        ep_hex[0] = bulk_in;
        ep_hex[1] = bulk_out;
        usb_hex_line(hexline, intf_hex, 3);
        kprintf("  [USB-DIAG] Interface Class/Subclass/Protocol=%s\n",
                hexline);
        usb_hex_line(hexline, ep_hex, 2);
        kprintf("  [USB-DIAG] Bulk IN/OUT=%s MaxPacket IN=%u OUT=%u\n",
                hexline, bulk_in_packet, bulk_out_packet);
    }
    if (!picked_intf || !bulk_in || !bulk_out ||
        bulk_in_packet == 0 || bulk_in_packet > 64 ||
        bulk_out_packet == 0 || bulk_out_packet > 64)
        return false;

    if (!usb_request(hc, addr, dd.max_packet0, false,
                     RT_HOST_TO_DEV | RT_STANDARD | RT_DEVICE,
                     REQ_SET_CONF, config_value, 0, 0, NULL))
        return false;
    active_config = 0xFFU;
    if (!usb_request(hc, addr, dd.max_packet0, false,
                     RT_DEV_TO_HOST | RT_STANDARD | RT_DEVICE,
                     REQ_GET_CONF, 0, 0, 1, &active_config))
        return false;
    kprintf("  [USB-DIAG] SET_CONFIGURATION value=%u GET_CONFIGURATION=%u\n",
            config_value, active_config);
    if (active_config != config_value) {
        kprintf("  [USB-DIAG] dispositivo no quedo configurado\n");
        return false;
    }
    if (picked_intf->alternate != 0U &&
        !usb_request(hc, addr, dd.max_packet0, false,
                     RT_HOST_TO_DEV | RT_STANDARD | RT_INTERFACE,
                     REQ_SET_INTERFACE, picked_intf->alternate,
                     picked_intf->number, 0, NULL))
        return false;
    /* Give old full-speed flash firmware time to leave Configured state
       setup and become ready for its first BOT command. */
    usb_delay_ms(250);

    kmemset(dev, 0, sizeof(*dev));
    dev->hc = hc;
    dev->addr = addr;
    dev->max_packet0 = dd.max_packet0;
    dev->bulk_in = bulk_in;
    dev->bulk_out = bulk_out;
    dev->interface_number = picked_intf->number;
    dev->interface_alternate = picked_intf->alternate;
    dev->bulk_in_packet = bulk_in_packet;
    dev->bulk_out_packet = bulk_out_packet;

    if (!usb_msc_get_max_lun(dev)) return false;
    /* Normalize a device inherited from a USB-aware BIOS before its first
       CBW.  BOT reset alone preserves toggles; the two CLEAR_FEATURE calls
       in reset-recovery deliberately synchronize both endpoints to DATA0. */
    if (!usb_msc_reset_recovery(dev)) {
        kprintf("  [USB-BOT] no se pudo sincronizar BOT inicialmente\n");
        return false;
    }

    if (!usb_msc_read_capacity(dev)) {
        kprintf("  [USB] UHCI mass storage sin capacidad valida\n");
        return false;
    }
    if (!usb_choose_name(dev->name)) return false;
    if (!block_register_ex(dev->name, BLOCK_DEVICE_USB, dev->sectors,
                           BLOCK_SECTOR_SIZE, false, dev,
                           usb_block_read))
        return false;
    block_set_writer(dev->name, usb_block_write);
    dev->registered = true;
    kprintf("  [USB] %s: UHCI mass storage %u sectores\n",
            dev->name, dev->sectors);
    return true;
}

static uint16_t uhci_port_addr(uhci_controller_t *hc, uint32_t port) {
    return (uint16_t)(hc->io_base + UHCI_PORTSC0 + (port * 2U));
}

static uint16_t uhci_port_sanitize(uint16_t status) {
    /* Preserve only writable control bits; never replay CCS/line status. */
    status &= UHCI_PORT_RW_MASK;
    status &= (uint16_t)~UHCI_PORT_RWC;
    return status;
}

static void uhci_write_port_control(uhci_controller_t *hc, uint32_t port,
                                    uint16_t control_bits) {
    uint16_t address = uhci_port_addr(hc, port);
    uint16_t value = uhci_port_sanitize(uhci_inw(address));
    value &= (uint16_t)~(UHCI_PORT_ENABLE |
                         UHCI_PORT_RESUME |
                         UHCI_PORT_RESET |
                         UHCI_PORT_SUSPEND);
    value |= UHCI_PORT_ALWAYS1 |
             (control_bits & (UHCI_PORT_ENABLE |
                              UHCI_PORT_RESUME |
                              UHCI_PORT_RESET |
                              UHCI_PORT_SUSPEND));
    uhci_outw(address, value);
    (void)uhci_inw(address);
}

static void uhci_clear_port_bits(uhci_controller_t *hc, uint32_t port,
                                 uint16_t bits) {
    uint16_t address = uhci_port_addr(hc, port);
    uint16_t status = uhci_port_sanitize(uhci_inw(address));

    status &= (uint16_t)~bits;
    /* Acknowledge only the requested write-one-to-clear bits. */
    status |= bits & UHCI_PORT_RWC;
    uhci_outw(address, status);
    (void)uhci_inw(address);
}

static void uhci_ack_port_changes(uhci_controller_t *hc, uint32_t port) {
    uint16_t status = uhci_inw(uhci_port_addr(hc, port));
    uint16_t changes = status & UHCI_PORT_RWC;
    if (changes) uhci_clear_port_bits(hc, port, changes);
}

static bool uhci_wait_port_state(uhci_controller_t *hc, uint32_t port,
                                 uint16_t mask, uint16_t expected,
                                 uint32_t attempts, uint32_t delay_ms) {
    uint16_t address = uhci_port_addr(hc, port);
    while (attempts--) {
        uint16_t status = uhci_inw(address);
        if ((status & mask) == expected) return true;
        usb_delay_ms(delay_ms);
    }
    return false;
}

static void uhci_log_port_state(uhci_controller_t *hc, const char *label) {
    if (!hc) return;
    for (uint32_t port = 0; port < hc->port_count; port++) {
        uint16_t status = uhci_inw(uhci_port_addr(hc, port));
        kprintf("  [USB-PORT] %s p%u=%x CCS=%u PED=%u LS=%u PR=%u SUSP=%u CHG=%x\n",
                label ? label : "?",
                port + 1U,
                status,
                (status & UHCI_PORT_CONNECT) != 0,
                (status & UHCI_PORT_ENABLE) != 0,
                (status & UHCI_PORT_LOW_SPEED) != 0,
                (status & UHCI_PORT_RESET) != 0,
                (status & UHCI_PORT_SUSPEND) != 0,
                status & UHCI_PORT_RWC);
    }
}

static bool uhci_reset_port(uhci_controller_t *hc, uint32_t port) {
    uint16_t address;
    uint16_t before;
    uint16_t asserted;
    uint16_t released;
    uint16_t status;
    uint16_t cmd;
    uint16_t reset_frame;
    uint16_t asserted_frame;

    if (!hc || port >= hc->port_count) return false;

    /*
     * Keep RS asserted while changing PORTSC.  The previous implementation
     * halted the PIIX4M first.  On the Latitude trace PR then read back as 1
     * while D+ remained high (J state), so the device never necessarily saw
     * the SE0 reset and continued using the address assigned by the BIOS.
     * Linux UHCI resets root ports with the controller running as well.
     */
    cmd = uhci_inw((uint16_t)(hc->io_base + UHCI_USBCMD));
    if (!(cmd & UHCI_CMD_RS) ||
        (uhci_inw((uint16_t)(hc->io_base + UHCI_USBSTS)) &
         UHCI_STS_HALTED)) {
        uhci_outw((uint16_t)(hc->io_base + UHCI_USBCMD), UHCI_CMD_RUN);
        if (!uhci_wait_halted(hc, false, 2000000U)) {
            kprintf("  [USB-DIAG] HC no corre antes de PORT RESET cmd=%x sts=%x\n",
                    cmd, uhci_inw((uint16_t)(hc->io_base + UHCI_USBSTS)));
            return false;
        }
        cmd = uhci_inw((uint16_t)(hc->io_base + UHCI_USBCMD));
    }

    address = uhci_port_addr(hc, port);
    before = uhci_inw(address);
    if (!(before & UHCI_PORT_CONNECT)) return false;

    /* USB 2.0 hub code debounces connect before reset.  PIIX4 hardware can
       report CCS early enough that the first address-0 SETUP gets only
       CRC/Timeout if we reset/enumerate immediately. */
    usb_delay_ms(USB_CONNECT_DEBOUNCE_MS);
    before = uhci_inw(address);
    if (!(before & UHCI_PORT_CONNECT)) return false;

    /* Never combine R/WC acknowledgement with a PORTSC control change. */
    uhci_ack_port_changes(hc, port);

    /* A retry may begin with PE still set.  Disable and unsuspend first so
       PR is asserted from a known root-hub state. */
    uhci_write_port_control(hc, port, 0);
    if (!uhci_wait_port_state(hc, port,
                              UHCI_PORT_ENABLE | UHCI_PORT_SUSPEND |
                              UHCI_PORT_RESUME | UHCI_PORT_RESET,
                              0, 20, 1))
        return false;
    usb_delay_ms(20);
    uhci_ack_port_changes(hc, port);

    reset_frame = uhci_inw((uint16_t)(hc->io_base + UHCI_FRNUM));
    uhci_write_port_control(hc, port, UHCI_PORT_RESET);
    if (!uhci_wait_port_state(hc, port, UHCI_PORT_RESET,
                              UHCI_PORT_RESET, 20, 1)) {
        kprintf("  [USB-DIAG] puerto %u no afirmo RESET portsc=%x\n",
                port, uhci_inw(address));
        return false;
    }
    usb_delay_ms(USB_RESET_ASSERT_MS);
    asserted = uhci_inw(address);
    asserted_frame = uhci_inw((uint16_t)(hc->io_base + UHCI_FRNUM));
    if (!(asserted & UHCI_PORT_RESET) ||
        (asserted & UHCI_PORT_ENABLE)) {
        kprintf("  [USB-DIAG] puerto %u RESET invalido portsc=%x\n",
                port, asserted);
        uhci_write_port_control(hc, port, 0);
        return false;
    }
    kprintf("  [USB-RESET] p%u running=%u cmd=%x frdelta=%u PR=%u line=%x se0=%u portsc=%x\n",
            port,
            (cmd & UHCI_CMD_RS) != 0,
            cmd,
            (asserted_frame - reset_frame) & 0x07FFU,
            (asserted & UHCI_PORT_RESET) != 0,
            (asserted >> 4) & 3U,
            (asserted & UHCI_PORT_LINE_MASK) == 0,
            asserted);
    uhci_write_port_control(hc, port, 0);
    if (!uhci_wait_port_state(hc, port, UHCI_PORT_RESET, 0, 20, 1)) {
        kprintf("  [USB-DIAG] puerto %u no libero RESET portsc=%x\n",
                port, uhci_inw(address));
        return false;
    }
    usb_delay_ms(20);
    released = uhci_inw(address);

    uhci_ack_port_changes(hc, port);
    if (!uhci_wait_port_state(hc, port, UHCI_PORT_CONNECT,
                              UHCI_PORT_CONNECT, 50, 2))
        return false;

    for (uint32_t retry = 0; retry < 10; retry++) {
        status = uhci_inw(address);
        if (!(status & UHCI_PORT_CONNECT)) return false;
        if (status & UHCI_PORT_ENABLE) break;
        uhci_write_port_control(hc, port, UHCI_PORT_ENABLE);
        usb_delay_ms(50);
        status = uhci_inw(address);
        if (status & (UHCI_PORT_CONNECT_CHG | UHCI_PORT_ENABLE_CHG)) {
            uhci_ack_port_changes(hc, port);
            usb_delay_ms(50);
        }
    }

    if (!uhci_wait_port_state(hc, port,
                              UHCI_PORT_CONNECT | UHCI_PORT_ENABLE,
                              UHCI_PORT_CONNECT | UHCI_PORT_ENABLE,
                              50, 2))
        return false;

    /* Keep emitting SOFs during the full recovery interval before EP0. */
    usb_delay_ms(100);
    usb_delay_ms(50);
    status = uhci_inw(address);
    if (!uhci_wait_port_state(hc, port, UHCI_PORT_LINE_MASK,
                              (status & UHCI_PORT_LOW_SPEED) ?
                                  (1U << 5) : (1U << 4),
                              20, 2)) {
        /* Some emulated UHCIs do not implement the read-only line bits. */
        kprintf("  [USB-DIAG] puerto %u line-status sin J (continua) portsc=%x\n",
                port, uhci_inw(address));
    }
    usb_delay_ms(USB_RESET_RECOVERY_MS);
    status = uhci_inw(address);
    kprintf("  [USB] UHCI puerto %u pre=%x reset=%x release=%x enable=%x speed=%s changes=%x line=%x cmd=%x\n",
            port, before, asserted, released, status,
            (status & UHCI_PORT_LOW_SPEED) ? "low" : "full",
            status & UHCI_PORT_RWC,
            (status >> 4) & 3U,
            uhci_inw((uint16_t)(hc->io_base + UHCI_USBCMD)));
    return (status & (UHCI_PORT_CONNECT | UHCI_PORT_ENABLE)) ==
               (UHCI_PORT_CONNECT | UHCI_PORT_ENABLE) &&
           !(status & UHCI_PORT_OVERCURRENT);
}

static uint16_t uhci_find_io_base(const pci_device_t *pci) {
    uint32_t raw;
    if (!pci) return 0;

    /* UHCI defines its I/O BAR at PCI offset 20h (BAR4). */
    raw = pci_config_read32(pci->bus, pci->slot, pci->function, 0x20);
    if ((raw & 1U) && (raw & 0xFFFFFFFCU))
        return (uint16_t)(raw & 0xFFFFFFE0U);

    /* Fallback for non-PIIX UHCI implementations. */
    for (uint8_t bar = 0; bar < PCI_BAR_COUNT; bar++) {
        raw = pci_config_read32(pci->bus, pci->slot, pci->function,
                                (uint8_t)(0x10 + bar * 4U));
        if ((raw & 1U) && (raw & 0xFFFFFFFCU))
            return (uint16_t)(raw & 0xFFFFFFE0U);
    }
    return 0;
}

static bool uhci_disable_legacy(const pci_device_t *pci) {
    uint16_t before;
    uint16_t cleared;
    uint16_t after;
    const uint16_t trap_enable_mask = 0x00BFU;
    if (!pci || pci->vendor_id != 0x8086U) return true;

    /*
     * First clear every legacy status/trap, then leave the register in the
     * stable Linux/Haiku state: only USBPIRQDEN (bit 13) enabled. USBINTR is
     * kept at zero, so routing PIRQ cannot generate an interrupt yet.
     */
    before = pci_config_read16(pci->bus, pci->slot,
                               pci->function, 0xC0);
    pci_config_write16(pci->bus, pci->slot, pci->function, 0xC0, 0x8F00U);
    cleared = pci_config_read16(pci->bus, pci->slot,
                                pci->function, 0xC0);
    pci_config_write16(pci->bus, pci->slot, pci->function, 0xC0, 0x2000U);
    after = pci_config_read16(pci->bus, pci->slot,
                              pci->function, 0xC0);
    kprintf("  [USB-DIAG] LEGSUP before=%x cleared=%x after=%x traps=%x pirq=%u\n",
            before, cleared, after, after & trap_enable_mask,
            (after & 0x2000U) != 0);
    if (after & trap_enable_mask) {
        kprintf("  [USB] UHCI BIOS/SMI legacy sigue habilitado\n");
        return false;
    }
    return true;
}

static void uhci_clear_intel_usbres(const pci_device_t *pci) {
    uint8_t before;
    uint8_t after;

    if (!pci || pci->vendor_id != 0x8086U) return;
    /* Linux uhci_pci_configure_hc() disables Intel's non-PME USB wakeup
       enables at PCI C4h before normal host-controller operation. */
    before = pci_config_read8(pci->bus, pci->slot, pci->function, 0xC4);
    pci_config_write8(pci->bus, pci->slot, pci->function, 0xC4, 0);
    after = pci_config_read8(pci->bus, pci->slot, pci->function, 0xC4);
    kprintf("  [USB-DIAG] USBRES_INTEL c4 before=%x after=%x\n",
            before, after);
}

static const char *uhci_piix4_stepping(const pci_device_t *pci,
                                       uint8_t rid0, uint8_t rid3) {
    if (!pci || pci->vendor_id != 0x8086U || pci->device_id != 0x7112U)
        return "no-PIIX4";
    if (rid0 == 0x00U && pci->revision_id == 0x00U)
        return "PIIX4-A0/A1";
    if (rid0 == 0x01U && pci->revision_id == 0x01U)
        return "PIIX4-B0";
    if (rid0 == 0x02U && pci->revision_id == 0x01U && rid3 == 0x02U)
        return "PIIX4E-A0";
    if (rid0 == 0x02U && pci->revision_id == 0x01U && rid3 == 0x03U)
        return "PIIX4M-A0";
    return "PIIX4-desconocido";
}

static void uhci_init_controller(const pci_device_t *pci) {
    uhci_controller_t *hc;
    uint16_t io_base;
    uint32_t timeout;
    uint8_t rid0;
    uint8_t rid3;
    uint8_t bios_sofmod;

    if (!pci || g_uhci_count >= UHCI_MAX_CONTROLLERS) return;
    io_base = uhci_find_io_base(pci);
    if (!io_base) {
        kprintf("  [USB] UHCI sin BAR de I/O vendor=%x device=%x\n",
                pci->vendor_id, pci->device_id);
        return;
    }
    rid0 = pci_config_read8(pci->bus, pci->slot, 0, 0x08);
    rid3 = pci_config_read8(pci->bus, pci->slot, 3, 0x08);
    kprintf("  [USB-DIAG] PCI rid f0=%x usb-f%u=%x f3=%x stepping=%s sbrn=%x latency=%x\n",
            rid0, pci->function, pci->revision_id, rid3,
            uhci_piix4_stepping(pci, rid0, rid3),
            pci_config_read8(pci->bus, pci->slot, pci->function, 0x60),
            pci_config_read8(pci->bus, pci->slot, pci->function, 0x0D));
    if (sizeof(uhci_td_t) != 32U || sizeof(uhci_qh_t) != 16U ||
        sizeof(usb_setup_t) != 8U ||
        sizeof(usb_msc_cbw_t) != MSC_CBW_WIRE_SIZE ||
        sizeof(usb_msc_csw_t) != MSC_CSW_WIRE_SIZE) {
        kprintf("  [USB] UHCI struct size invalido td=%u qh=%u setup=%u cbw=%u csw=%u\n",
                sizeof(uhci_td_t), sizeof(uhci_qh_t),
                sizeof(usb_setup_t), sizeof(usb_msc_cbw_t),
                sizeof(usb_msc_csw_t));
        return;
    }

    /* Disable BIOS traps before touching the live I/O schedule.  Keep the
       older ordering: enable bus mastering only after our FLBASE is valid. */
    if (!uhci_disable_legacy(pci)) return;
    uhci_clear_intel_usbres(pci);
    if (!pci_enable_command(pci, PCI_COMMAND_IO)) {
        kprintf("  [USB] UHCI no pudo habilitar PCI I/O\n");
        return;
    }
    {
        uint16_t pci_cmd = pci_config_read16(pci->bus, pci->slot,
                                             pci->function, 0x04);
        kprintf("  [USB-DIAG] PCI command inicial=%x io=%u busmaster=%u\n",
                pci_cmd, (pci_cmd & PCI_COMMAND_IO) != 0,
                (pci_cmd & PCI_COMMAND_BUSMASTER) != 0);
        if (!(pci_cmd & PCI_COMMAND_IO))
            return;
    }

    /* UHCI 1.1 section 2.1.6 requires software to preserve the BIOS value:
       SOFMOD is board/clock dependent and GRESET/HCRESET reset it. */
    bios_sofmod = inb((uint16_t)(io_base + UHCI_SOFMOD)) & 0x7FU;

    hc = &g_uhci[g_uhci_count];
    kmemset(hc, 0, sizeof(*hc));
    hc->io_base = io_base;
    hc->port_count = UHCI_ROOT_PORTS;

    uhci_outw((uint16_t)(io_base + UHCI_USBINTR), 0);
    uhci_outw((uint16_t)(io_base + UHCI_USBCMD), 0);
    if (!uhci_wait_halted(hc, true, 1000000U)) {
        kprintf("  [USB] UHCI no se detuvo antes del reset io=%x sts=%x\n",
                io_base, uhci_inw((uint16_t)(io_base + UHCI_USBSTS)));
        return;
    }

    uhci_outw((uint16_t)(io_base + UHCI_USBCMD), UHCI_CMD_GRESET);
    usb_delay_ms(20);
    uhci_outw((uint16_t)(io_base + UHCI_USBCMD), 0);
    usb_delay_ms(10);

    uhci_outw((uint16_t)(io_base + UHCI_USBCMD), UHCI_CMD_HCRESET);
    timeout = 1000000U;
    while (timeout &&
           (uhci_inw((uint16_t)(io_base + UHCI_USBCMD)) &
            UHCI_CMD_HCRESET))
        timeout--;
    if (!timeout &&
        (uhci_inw((uint16_t)(io_base + UHCI_USBCMD)) &
         UHCI_CMD_HCRESET)) {
        kprintf("  [USB] UHCI reset timeout io=%x\n", io_base);
        return;
    }

    /* HCRESET does not clear Suspend/Reset/Resume Detect on every UHCI.
       Clear both root ports so the other port cannot retain a K-state. */
    for (uint32_t port = 0; port < hc->port_count; port++) {
        uhci_write_port_control(hc, port, 0);
        uhci_ack_port_changes(hc, port);
    }
    usb_delay_ms(2);

    hc->frame_list = g_uhci_frame_lists[g_uhci_count];
    hc->qh = &g_uhci_qhs[g_uhci_count];
    hc->xfer_qh = &g_uhci_xfer_qhs[g_uhci_count];
    hc->term_td = &g_uhci_term_tds[g_uhci_count];
    hc->td_pool = g_uhci_td_pools[g_uhci_count];
    hc->setup_buffer = g_uhci_setup_buffers[g_uhci_count];
    hc->control_buffer = g_uhci_control_buffers[g_uhci_count];

    /* The kernel currently runs without paging, so these identity-mapped
       addresses are also the physical addresses programmed into UHCI. */
    if ((uint32_t)(uintptr_t)hc->frame_list >= 0x01000000U ||
        (uint32_t)(uintptr_t)hc->qh >= 0x01000000U ||
        (uint32_t)(uintptr_t)hc->xfer_qh >= 0x01000000U ||
        (uint32_t)(uintptr_t)hc->term_td >= 0x01000000U ||
        (uint32_t)(uintptr_t)hc->td_pool >= 0x01000000U ||
        (uint32_t)(uintptr_t)hc->setup_buffer >= 0x01000000U ||
        (uint32_t)(uintptr_t)hc->control_buffer >= 0x01000000U) {
        kprintf("  [USB] UHCI DMA fuera de memoria baja frame=%x qh=%x xqh=%x term=%x td=%x\n",
                (uint32_t)(uintptr_t)hc->frame_list,
                (uint32_t)(uintptr_t)hc->qh,
                (uint32_t)(uintptr_t)hc->xfer_qh,
                (uint32_t)(uintptr_t)hc->term_td,
                (uint32_t)(uintptr_t)hc->td_pool);
        return;
    }

    kmemset(hc->frame_list, 0,
            UHCI_FRAME_COUNT * sizeof(hc->frame_list[0]));
    kmemset(hc->qh, 0, sizeof(*hc->qh));
    kmemset(hc->xfer_qh, 0, sizeof(*hc->xfer_qh));
    kmemset(hc->term_td, 0, sizeof(*hc->term_td));
    kmemset(hc->td_pool, 0, UHCI_MAX_TD * sizeof(hc->td_pool[0]));
    kmemset(hc->setup_buffer, 0, 16U);
    kmemset(hc->control_buffer, 0, UHCI_CTRL_DMA_SIZE);
    hc->term_td->link = UHCI_LINK_TERM;
    hc->term_td->status = 0;
    hc->term_td->token = uhci_token(UHCI_PID_IN, 0x7FU, 0, 0, 0);
    hc->term_td->buffer = 0;
    hc->term_td->next = 0;
    hc->term_td->used = true;
    uhci_park_schedule(hc);
    for (uint32_t frame = 0; frame < UHCI_FRAME_COUNT; frame++) {
        hc->frame_list[frame] =
            (uint32_t)(uintptr_t)hc->qh | UHCI_LINK_QH;
    }

    uhci_dma_fence();
    outl((uint16_t)(io_base + UHCI_FLBASEADD),
         (uint32_t)(uintptr_t)hc->frame_list);
    {
        uint32_t flbase_read = inl((uint16_t)(io_base + UHCI_FLBASEADD));
        if ((flbase_read & 0xFFFFF000U) !=
            ((uint32_t)(uintptr_t)hc->frame_list & 0xFFFFF000U)) {
            kprintf("  [USB-DIAG] FLBASE mismatch wrote=%x read=%x\n",
                    (uint32_t)(uintptr_t)hc->frame_list, flbase_read);
            return;
        }
        if (flbase_read & 0xFFFU)
            kprintf("  [USB-DIAG] FLBASE reserved bits=%x (base valida=%x)\n",
                    flbase_read & 0xFFFU, flbase_read & 0xFFFFF000U);
    }
    uhci_outw((uint16_t)(io_base + UHCI_FRNUM), 0);
    outb((uint16_t)(io_base + UHCI_SOFMOD), bios_sofmod);
    uhci_outw((uint16_t)(io_base + UHCI_USBSTS), UHCI_STS_CLEAR);
    uhci_outw((uint16_t)(io_base + UHCI_USBINTR), 0);
    if (!pci_enable_command(pci, PCI_COMMAND_BUSMASTER) ||
        !(pci_config_read16(pci->bus, pci->slot, pci->function, 0x04) &
          PCI_COMMAND_BUSMASTER)) {
        kprintf("  [USB] UHCI no pudo habilitar PCI busmaster\n");
        return;
    }
    uhci_outw((uint16_t)(io_base + UHCI_USBCMD),
              UHCI_CMD_RUN);
    usb_delay_ms(20);

    if (uhci_inw((uint16_t)(io_base + UHCI_USBSTS)) &
        (UHCI_STS_HSE | UHCI_STS_HCPE | UHCI_STS_HALTED)) {
        kprintf("  [USB] UHCI no pudo iniciar io=%x sts=%x\n",
                io_base,
                uhci_inw((uint16_t)(io_base + UHCI_USBSTS)));
        return;
    }

    hc->ready = true;
    kprintf("  [USB] UHCI 2 puertos en I/O %x vendor=%x device=%x\n",
            io_base, pci->vendor_id, pci->device_id);
    kprintf("  [USB-DIAG] frame=%x qh=%x xqh=%x term=%x tdpool=%x setup=%x ctrl=%x\n",
            (uint32_t)(uintptr_t)hc->frame_list,
            (uint32_t)(uintptr_t)hc->qh,
            (uint32_t)(uintptr_t)hc->xfer_qh,
            (uint32_t)(uintptr_t)hc->term_td,
            (uint32_t)(uintptr_t)hc->td_pool,
            (uint32_t)(uintptr_t)hc->setup_buffer,
            (uint32_t)(uintptr_t)hc->control_buffer);
    kprintf("  [USB-DIAG] align frame=%u qh=%u xqh=%u term=%u td=%u setup=%u ctrl=%u\n",
            (uint32_t)(uintptr_t)hc->frame_list & (UHCI_FRAME_ALIGN - 1U),
            (uint32_t)(uintptr_t)hc->qh & (UHCI_QH_ALIGN - 1U),
            (uint32_t)(uintptr_t)hc->xfer_qh & (UHCI_QH_ALIGN - 1U),
            (uint32_t)(uintptr_t)hc->term_td & (UHCI_TD_ALIGN - 1U),
            (uint32_t)(uintptr_t)hc->td_pool & (UHCI_TD_ALIGN - 1U),
            (uint32_t)(uintptr_t)hc->setup_buffer & 15U,
            (uint32_t)(uintptr_t)hc->control_buffer & 63U);
    kprintf("  [USB-DIAG] PIIX-term link=%x sts=%x token=%x buf=%x\n",
            hc->term_td->link, hc->term_td->status,
            hc->term_td->token, hc->term_td->buffer);
    kprintf("  [USB-DIAG] SOFMOD bios=%x active=%x\n",
            bios_sofmod,
            inb((uint16_t)(io_base + UHCI_SOFMOD)) & 0x7FU);
    kprintf("  [USB-DIAG] cmd=%x sts=%x fr=%u flbase=%x legsup=%x\n",
            uhci_inw((uint16_t)(io_base + UHCI_USBCMD)),
            uhci_inw((uint16_t)(io_base + UHCI_USBSTS)),
            uhci_inw((uint16_t)(io_base + UHCI_FRNUM)),
            inl((uint16_t)(io_base + UHCI_FLBASEADD)),
            pci_config_read16(pci->bus, pci->slot, pci->function, 0xC0));

    uhci_log_port_state(hc, "before-scan");
    for (uint32_t port = 0; port < hc->port_count; port++) {
        if (uhci_reset_port(hc, port)) {
            uhci_log_port_state(hc, "after-reset");
            if (uhci_inw(uhci_port_addr(hc, port)) &
                UHCI_PORT_LOW_SPEED) {
                usb_probe_low_speed_ep0(hc, port);
                continue;
            }
            if (usb_try_mass_storage(hc, &g_mass[g_uhci_count], port))
                break;
        }
    }
    g_uhci_count++;
}

static void usb_uhci_scan_ports(void) {
    for (uint32_t index = 0; index < g_uhci_count; index++) {
        uhci_controller_t *hc = &g_uhci[index];
        uhci_mass_device_t *dev = &g_mass[index];
        if (!hc->ready || dev->registered) continue;
        for (uint32_t port = 0; port < hc->port_count; port++) {
            uint8_t bit = (uint8_t)(1U << port);
            uint16_t portsc = uhci_inw(uhci_port_addr(hc, port));
            if (!(portsc & UHCI_PORT_CONNECT)) {
                hc->low_speed_probe_mask &= (uint8_t)~bit;
                hc->swdbg_done_mask &= (uint8_t)~bit;
                continue;
            }
            if ((portsc & UHCI_PORT_LOW_SPEED) &&
                (hc->low_speed_probe_mask & bit))
                continue;
            if (uhci_reset_port(hc, port)) {
                uhci_log_port_state(hc, "hotplug-reset");
                if (uhci_inw(uhci_port_addr(hc, port)) &
                    UHCI_PORT_LOW_SPEED) {
                    usb_probe_low_speed_ep0(hc, port);
                    continue;
                }
                if (usb_try_mass_storage(hc, dev, port)) break;
            }
        }
    }
}

static void usb_uhci_hotplug_main(void *arg UNUSED) {
    for (;;) {
        usb_uhci_scan_ports();
        task_sleep(100);
    }
}

void usb_uhci_init(void) {
    int index = -1;
    g_uhci_count = 0;
    g_uhci_fail_log_budget = 32;
    g_uhci_bot_trace_budget = 12;
    g_uhci_td_trace_budget = 24;
    kmemset(g_uhci, 0, sizeof(g_uhci));
    kmemset(g_mass, 0, sizeof(g_mass));

    for (;;) {
        index = pci_find_by_class(0x0C, 0x03, (uint32_t)(index + 1));
        if (index < 0) break;
        const pci_device_t *dev = pci_device_at((uint32_t)index);
        if (!dev || dev->prog_if != 0x00) continue;
        kprintf("  [USB] PCI UHCI %x:%x.%x vendor=%x device=%x rev=%x\n",
                dev->bus, dev->slot, dev->function,
                dev->vendor_id, dev->device_id, dev->revision_id);
        uhci_init_controller(dev);
    }
}

void usb_uhci_start_hotplug_task(void) {
    if (g_uhci_count == 0) return;
    (void)task_create("uhci-hotplug", usb_uhci_hotplug_main, NULL);
}
