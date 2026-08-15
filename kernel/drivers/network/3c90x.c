#include "../../include/types.h"
#include "../../include/memory.h"
#include "../../include/pci.h"
#include "../../include/pic.h"
#include "../../include/task.h"
#include "../../include/pit.h"
#include "../../include/network.h"
#include "../../include/driver.h"
#include "../../include/vga.h"

#define XL_VENDOR 0x10B7U
#define RX_RING_SIZE 8U
#define RX_BUFFER_SIZE 1536U
#define LAST_FRAGMENT 0x80000000U
#define RX_COMPLETE 0x00008000U
#define RX_ERROR    0x00004000U

#define REG_COMMAND  0x0EU
#define REG_TX_STATUS 0x1BU
#define REG_PKT_STATUS 0x20U
#define REG_DOWN_LIST 0x24U
#define REG_UP_LIST   0x38U

#define CMD_TOTAL_RESET  (0U << 11)
#define CMD_SELECT_W     (1U << 11)
#define CMD_RX_DISABLE   (3U << 11)
#define CMD_RX_ENABLE    (4U << 11)
#define CMD_RX_RESET     (5U << 11)
#define CMD_UP_STALL     (6U << 11)
#define CMD_UP_UNSTALL    ((6U << 11) + 1U)
#define CMD_DOWN_STALL  ((6U << 11) + 2U)
#define CMD_DOWN_UNSTALL ((6U << 11) + 3U)
#define CMD_TX_ENABLE    (9U << 11)
#define CMD_TX_DISABLE  (10U << 11)
#define CMD_TX_RESET    (11U << 11)
#define CMD_ACK_INTR    (13U << 11)
#define CMD_INTR_ENABLE (14U << 11)
#define CMD_STATUS_EN   (15U << 11)
#define CMD_RX_FILTER   (16U << 11)
#define CMD_RX_THRESHOLD (17U << 11)
#define CMD_STATS_ENABLE (21U << 11)
#define CMD_STATS_DISABLE (22U << 11)
#define STATUS_CMD_BUSY 0x1000U

typedef struct {
    uint32_t next, status, address, length;
} __attribute__((packed, aligned(8))) xl_descriptor_t;

typedef struct {
    uint16_t io;
    uint16_t device_id;
    uint8_t mac[6];
    volatile xl_descriptor_t *rx;
    void *rx_allocation;
    uint8_t *rx_buffers[RX_RING_SIZE];
    uint32_t rx_index;
    volatile xl_descriptor_t *tx;
    void *tx_allocation;
    uint8_t *tx_buffer;
    volatile bool tx_busy;
    volatile bool running;
    int poll_pid;
    const pci_device_t *pci;
} xl_state_t;

static xl_state_t g_xl;

static uint16_t port_in16(uint16_t port) {
    uint16_t value; __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port)); return value;
}
static void port_out16(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}
static void command(uint16_t value) { port_out16((uint16_t)(g_xl.io + REG_COMMAND), value); }
static bool command_wait(void) {
    for (uint32_t i = 0; i < 100000U; i++)
        if (!(port_in16((uint16_t)(g_xl.io + REG_COMMAND)) & STATUS_CMD_BUSY)) return true;
    return false;
}
static bool issue(uint16_t value) { command(value); return command_wait(); }
static void select_window(uint8_t window) { issue((uint16_t)(CMD_SELECT_W | window)); }

static bool supported_id(uint16_t id) {
    static const uint16_t ids[] = {
        0x9000,0x9001,0x9004,0x9005,0x9006,0x900A,
        0x9050,0x9051,0x9054,0x9055,0x9056,0x9058,0x905A,
        0x9200,0x9201,0x9202,0x9210,0x9800,0x9805,0x7646,
        0x5055,0x6055,0x6056,0x5B57,0x5057,0x5157,0x5257,
        0x6560,0x6562,0x6564,0x4500,0x1201,0x1202
    };
    for (uint32_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) if (ids[i] == id) return true;
    return false;
}

static uint16_t eeprom_base(void) {
    if (g_xl.device_id == 0x5055U || g_xl.device_id == 0x6055U ||
        g_xl.device_id == 0x5B57U || g_xl.device_id == 0x5057U ||
        g_xl.device_id == 0x5157U || g_xl.device_id == 0x5257U ||
        g_xl.device_id == 0x6560U || g_xl.device_id == 0x6562U ||
        g_xl.device_id == 0x6564U) return 0x230U;
    if (g_xl.device_id == 0x6056U) return 0x00B0U;
    return 0x0080U;
}
static bool eeprom_read(uint8_t index, uint16_t *value) {
    select_window(0); port_out16((uint16_t)(g_xl.io + 0x0A), (uint16_t)(eeprom_base() + index));
    for (uint32_t i = 0; i < 200000U; i++) {
        if (!(port_in16((uint16_t)(g_xl.io + 0x0A)) & 0x8000U)) {
            *value = port_in16((uint16_t)(g_xl.io + 0x0C)); return true;
        }
        io_wait();
    }
    return false;
}
static bool read_mac(void) {
    for (uint8_t i = 0; i < 3; i++) {
        uint16_t word; if (!eeprom_read((uint8_t)(10U + i), &word)) return false;
        g_xl.mac[i * 2U] = (uint8_t)(word >> 8); g_xl.mac[i * 2U + 1U] = (uint8_t)word;
    }
    if ((g_xl.mac[0] & 1U) || !(g_xl.mac[0] | g_xl.mac[1] | g_xl.mac[2] |
        g_xl.mac[3] | g_xl.mac[4] | g_xl.mac[5])) return false;
    return true;
}

static void cardbus_power_quirks(void) {
    if (g_xl.device_id != 0x6055U && g_xl.device_id != 0x6056U) return;
    select_window(2);
    { uint16_t reset = port_in16((uint16_t)(g_xl.io + 0x0C));
      reset = (uint16_t)((reset & ~0x4010U) | 0x4000U);
      port_out16((uint16_t)(g_xl.io + 0x0C), reset); }
    if (g_xl.device_id == 0x6056U) { select_window(0); port_out16(g_xl.io, 0x0800U); }
    if (g_xl.pci->bars[2] && !(g_xl.pci->bars[2] & 1U)) {
        volatile uint32_t *function_status = (volatile uint32_t *)(uintptr_t)((g_xl.pci->bars[2] & ~0x0FU) + 4U);
        *function_status = 0x8000U;
    }
}

static bool allocate_rings(void) {
    uintptr_t aligned;
    g_xl.rx_allocation = kzalloc(sizeof(xl_descriptor_t) * RX_RING_SIZE + 7U);
    g_xl.tx_allocation = kzalloc(sizeof(xl_descriptor_t) + 7U);
    aligned = ((uintptr_t)g_xl.rx_allocation + 7U) & ~7U;
    g_xl.rx = (volatile xl_descriptor_t *)aligned;
    aligned = ((uintptr_t)g_xl.tx_allocation + 7U) & ~7U;
    g_xl.tx = (volatile xl_descriptor_t *)aligned;
    g_xl.tx_buffer = (uint8_t *)kmalloc(RX_BUFFER_SIZE);
    if (!g_xl.rx || !g_xl.tx || !g_xl.tx_buffer) return false;
    for (uint32_t i = 0; i < RX_RING_SIZE; i++) {
        g_xl.rx_buffers[i] = (uint8_t *)kmalloc(RX_BUFFER_SIZE);
        if (!g_xl.rx_buffers[i]) return false;
        g_xl.rx[i].next = (uint32_t)(uintptr_t)&g_xl.rx[(i + 1U) % RX_RING_SIZE];
        g_xl.rx[i].address = (uint32_t)(uintptr_t)g_xl.rx_buffers[i];
        g_xl.rx[i].length = RX_BUFFER_SIZE | LAST_FRAGMENT;
        g_xl.rx[i].status = 0;
    }
    return true;
}

static void release_rings(void) {
    for (uint32_t i = 0; i < RX_RING_SIZE; i++) if (g_xl.rx_buffers[i]) kfree(g_xl.rx_buffers[i]);
    if (g_xl.rx_allocation) kfree(g_xl.rx_allocation);
    if (g_xl.tx_allocation) kfree(g_xl.tx_allocation);
    if (g_xl.tx_buffer) kfree(g_xl.tx_buffer);
    g_xl.rx = NULL; g_xl.tx = NULL; g_xl.tx_buffer = NULL;
    g_xl.rx_allocation = NULL; g_xl.tx_allocation = NULL;
}

static void receive_poll(void) {
    for (uint32_t work = 0; work < RX_RING_SIZE; work++) {
        volatile xl_descriptor_t *descriptor = &g_xl.rx[g_xl.rx_index];
        uint32_t status = descriptor->status;
        if (!(status & RX_COMPLETE)) break;
        if (!(status & RX_ERROR)) {
            uint16_t length = (uint16_t)(status & 0x1FFFU);
            if (length >= 14U && length <= RX_BUFFER_SIZE)
                netdev_receive(g_xl.rx_buffers[g_xl.rx_index], length);
        }
        descriptor->status = 0;
        g_xl.rx_index = (g_xl.rx_index + 1U) % RX_RING_SIZE;
    }
    if (inl((uint16_t)(g_xl.io + REG_UP_LIST)) == 0U) {
        outl((uint16_t)(g_xl.io + REG_UP_LIST), (uint32_t)(uintptr_t)&g_xl.rx[g_xl.rx_index]);
        command(CMD_UP_UNSTALL);
    }
}
static void poll_task(void *argument UNUSED) {
    uint32_t interval = (pit_get_frequency_hz() + 99U) / 100U;
    if (!interval) interval = 1U;
    /* Las esperas activas del stack llaman netdev_poll() directamente. En
     * background, 100 Hz basta para RX y evita 300 cambios de tarea/s. */
    while (g_xl.running) { netdev_poll(); task_sleep(interval); }
    task_exit();
}

static bool xl_send(const void *frame, uint16_t length) {
    uint32_t start, limit;
    if (!g_xl.running || !frame || length < 14U || length > 1514U || g_xl.tx_busy) return false;
    g_xl.tx_busy = true; kmemcpy(g_xl.tx_buffer, frame, length);
    g_xl.tx->next = 0; g_xl.tx->status = (uint32_t)length | 0x80000000U;
    g_xl.tx->address = (uint32_t)(uintptr_t)g_xl.tx_buffer;
    g_xl.tx->length = (uint32_t)length | LAST_FRAGMENT;
    issue(CMD_DOWN_STALL);
    outl((uint16_t)(g_xl.io + REG_DOWN_LIST), (uint32_t)(uintptr_t)g_xl.tx);
    command(CMD_DOWN_UNSTALL);
    start = pit_get_ticks(); limit = pit_get_frequency_hz(); if (!limit) limit = 100U;
    while (inl((uint16_t)(g_xl.io + REG_DOWN_LIST)) != 0U &&
           (uint32_t)(pit_get_ticks() - start) < limit) task_yield();
    g_xl.tx_busy = false;
    if (inl((uint16_t)(g_xl.io + REG_DOWN_LIST)) != 0U) {
        issue(CMD_TX_RESET); command(CMD_TX_ENABLE); return false;
    }
    while (inb((uint16_t)(g_xl.io + REG_TX_STATUS)) & 0x3CU)
        outb((uint16_t)(g_xl.io + REG_TX_STATUS), 0);
    return true;
}

static bool xl_link_up(void) {
    uint16_t media; uint32_t config; uint8_t transceiver;
    select_window(4); media = port_in16((uint16_t)(g_xl.io + 0x0A));
    select_window(3); config = inl(g_xl.io); transceiver = (uint8_t)((config >> 20) & 0x0FU);
    return (media & 0x0800U) != 0U || transceiver == 6U || transceiver == 8U || transceiver == 9U;
}

static const net_device_ops_t g_net_ops = {
    "eth0-3c90x", g_xl.mac, 1500U, xl_send, xl_link_up, receive_poll
};

static bool xl_init(void) {
    const pci_device_t *device = NULL;
    kmemset(&g_xl, 0, sizeof(g_xl));
    for (uint32_t i = 0; i < pci_device_count(); i++) {
        const pci_device_t *candidate = pci_device_at(i);
        if (candidate && candidate->vendor_id == XL_VENDOR && supported_id(candidate->device_id)) { device = candidate; break; }
    }
    if (!device || !(device->bars[0] & 1U)) return false;
    g_xl.pci = device; g_xl.device_id = device->device_id;
    g_xl.io = (uint16_t)(device->bars[0] & ~3U);
    pci_enable_command(device, PCI_COMMAND_IO | PCI_COMMAND_BUSMASTER);
    if (!issue(CMD_TOTAL_RESET) || !read_mac() || !allocate_rings()) { release_rings(); return false; }
    cardbus_power_quirks();
    select_window(2);
    for (uint8_t i = 0; i < 6; i++) outb((uint16_t)(g_xl.io + i), g_xl.mac[i]);
    issue(CMD_RX_DISABLE); issue(CMD_TX_DISABLE); issue(CMD_RX_RESET); issue(CMD_TX_RESET);
    command(CMD_INTR_ENABLE); command(CMD_STATUS_EN); command((uint16_t)(CMD_ACK_INTR | 0x07FFU));
    command((uint16_t)(CMD_RX_THRESHOLD | (RX_BUFFER_SIZE >> 2)));
    outl((uint16_t)(g_xl.io + REG_PKT_STATUS), 0x20U);
    outl((uint16_t)(g_xl.io + REG_UP_LIST), (uint32_t)(uintptr_t)g_xl.rx);
    outl((uint16_t)(g_xl.io + REG_DOWN_LIST), 0);
    command((uint16_t)(CMD_RX_FILTER | 1U | 4U)); command(CMD_STATS_ENABLE);
    command(CMD_RX_ENABLE); command(CMD_TX_ENABLE);
    g_xl.running = true;
    if (!netdev_register(&g_net_ops)) { g_xl.running = false; release_rings(); return false; }
    g_xl.poll_pid = task_create("3c90x-rx", poll_task, NULL);
    if (g_xl.poll_pid < 0) { netdev_unregister(&g_net_ops); g_xl.running = false; release_rings(); return false; }
    kprintf("  [3C90X] PCI %x:%x IO=0x%x MAC=%x:%x:%x:%x:%x:%x polling\n",
            device->vendor_id, device->device_id, g_xl.io, g_xl.mac[0], g_xl.mac[1],
            g_xl.mac[2], g_xl.mac[3], g_xl.mac[4], g_xl.mac[5]);
    return true;
}

static void xl_shutdown(void) {
    if (!g_xl.running) return;
    g_xl.running = false;
    netdev_unregister(&g_net_ops);
    command(CMD_RX_DISABLE); command(CMD_TX_DISABLE); outl((uint16_t)(g_xl.io + REG_UP_LIST), 0);
    outl((uint16_t)(g_xl.io + REG_DOWN_LIST), 0); release_rings();
}

const bk_driver_module_t *bleskernos_driver_query(void) {
    static const bk_driver_module_t module = { BK_DRIVER_ABI_VERSION, sizeof(bk_driver_module_t),
        "3c90x", "3Com EtherLink XL 3c900/3c905/3c556", xl_init, xl_shutdown };
    return &module;
}
