#include "../../include/types.h"
#include "../../include/memory.h"
#include "../../include/pci.h"
#include "../../include/pic.h"
#include "../../include/pit.h"
#include "../../include/task.h"
#include "../../include/network.h"
#include "../../include/driver.h"
#include "../../include/vga.h"

#define RTL_VENDOR_REALTEK 0x10ECU
#define RTL_DEVICE_8139    0x8139U
#define RTL_DEVICE_8138    0x8138U
#define RTL_DEVICE_8129    0x8129U

#define RTL_RX_RING_SIZE 8192U
#define RTL_RX_ALLOC_SIZE (RTL_RX_RING_SIZE + 16U + 1536U)
#define RTL_TX_SLOTS 4U
#define RTL_TX_BUFFER_SIZE 1536U

#define RTL_MAC0       0x00U
#define RTL_TX_STATUS  0x10U
#define RTL_TX_ADDRESS 0x20U
#define RTL_RX_BUFFER  0x30U
#define RTL_COMMAND    0x37U
#define RTL_CAPR       0x38U
#define RTL_IMR        0x3CU
#define RTL_ISR        0x3EU
#define RTL_RCR        0x44U
#define RTL_CONFIG1    0x52U
#define RTL_BMSR       0x64U

#define RTL_CMD_RX_EMPTY 0x01U
#define RTL_CMD_TX_ENABLE 0x04U
#define RTL_CMD_RX_ENABLE 0x08U
#define RTL_CMD_RESET 0x10U
#define RTL_RX_OK 0x0001U
#define RTL_TX_OWN 0x00002000U
#define RTL_TX_OK  0x00008000U

typedef struct {
    uint16_t io;
    uint8_t mac[6];
    uint8_t *rx_ring;
    uint32_t rx_offset;
    uint8_t *tx_buffer[RTL_TX_SLOTS];
    uint8_t tx_slot;
    volatile bool running;
    const pci_device_t *pci;
} rtl8139_state_t;

static rtl8139_state_t g_rtl;

static uint16_t in16(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static void out16(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static bool rtl_id_supported(uint16_t vendor, uint16_t device) {
    if (vendor == RTL_VENDOR_REALTEK &&
        (device == RTL_DEVICE_8139 || device == RTL_DEVICE_8138 ||
         device == RTL_DEVICE_8129)) return true;
    /* Clones frecuentes con el núcleo 8139 clásico. */
    return (vendor == 0x1113U && device == 0x1211U) ||
           (vendor == 0x1186U && device == 0x1300U) ||
           (vendor == 0x018AU && device == 0x0106U) ||
           (vendor == 0x021BU && device == 0x8139U);
}

static bool rtl_reset(void) {
    outb((uint16_t)(g_rtl.io + RTL_CONFIG1), 0x00U);
    outb((uint16_t)(g_rtl.io + RTL_COMMAND), RTL_CMD_RESET);
    for (uint32_t i = 0; i < 1000000U; i++) {
        if (!(inb((uint16_t)(g_rtl.io + RTL_COMMAND)) & RTL_CMD_RESET))
            return true;
        io_wait();
    }
    return false;
}

static void rtl_copy_from_ring(uint8_t *destination, uint32_t offset,
                               uint32_t length) {
    while (length--) {
        *destination++ = g_rtl.rx_ring[offset];
        offset = (offset + 1U) & (RTL_RX_RING_SIZE - 1U);
    }
}

static void rtl_receive_poll(void) {
    uint32_t work = 32U;
    while (work-- && !(inb((uint16_t)(g_rtl.io + RTL_COMMAND)) & RTL_CMD_RX_EMPTY)) {
        uint32_t offset = g_rtl.rx_offset;
        uint16_t status = (uint16_t)(g_rtl.rx_ring[offset] |
                          ((uint16_t)g_rtl.rx_ring[(offset + 1U) & 8191U] << 8));
        uint16_t wire_length = (uint16_t)(g_rtl.rx_ring[(offset + 2U) & 8191U] |
                               ((uint16_t)g_rtl.rx_ring[(offset + 3U) & 8191U] << 8));
        if (!(status & RTL_RX_OK) || wire_length < 18U || wire_length > 1522U) {
            /* Un encabezado corrupto desincroniza el anillo: recuperar la NIC. */
            outb((uint16_t)(g_rtl.io + RTL_COMMAND), RTL_CMD_TX_ENABLE);
            outb((uint16_t)(g_rtl.io + RTL_COMMAND),
                 RTL_CMD_RX_ENABLE | RTL_CMD_TX_ENABLE);
            g_rtl.rx_offset = 0;
            out16((uint16_t)(g_rtl.io + RTL_CAPR), 0xFFF0U);
            return;
        }
        {
            uint8_t frame[NET_FRAME_MAX];
            uint16_t frame_length = (uint16_t)(wire_length - 4U); /* quitar FCS */
            rtl_copy_from_ring(frame, (offset + 4U) & 8191U, frame_length);
            netdev_receive(frame, frame_length);
        }
        g_rtl.rx_offset = (offset + 4U + wire_length + 3U) & ~3U;
        g_rtl.rx_offset &= (RTL_RX_RING_SIZE - 1U);
        out16((uint16_t)(g_rtl.io + RTL_CAPR),
              (uint16_t)((g_rtl.rx_offset - 16U) & 0xFFFFU));
    }
    {
        uint16_t interrupts = in16((uint16_t)(g_rtl.io + RTL_ISR));
        if (interrupts) out16((uint16_t)(g_rtl.io + RTL_ISR), interrupts);
    }
}

static void rtl_poll_task(void *argument UNUSED) {
    uint32_t interval = (pit_get_frequency_hz() + 99U) / 100U;
    if (!interval) interval = 1U;
    while (g_rtl.running) {
        netdev_poll();
        task_sleep(interval);
    }
    task_exit();
}

static bool rtl_send(const void *frame, uint16_t length) {
    uint8_t slot;
    uint16_t transmit_length;
    uint32_t start, timeout;
    if (!g_rtl.running || !frame || length < 14U || length > 1514U) return false;
    slot = g_rtl.tx_slot;
    start = pit_get_ticks();
    timeout = pit_get_frequency_hz();
    if (!timeout) timeout = 100U;
    while (!(inl((uint16_t)(g_rtl.io + RTL_TX_STATUS + slot * 4U)) & RTL_TX_OWN)) {
        if ((uint32_t)(pit_get_ticks() - start) >= timeout) return false;
        task_yield();
    }
    kmemcpy(g_rtl.tx_buffer[slot], frame, length);
    transmit_length = length < 60U ? 60U : length;
    if (transmit_length > length)
        kmemset(g_rtl.tx_buffer[slot] + length, 0, transmit_length - length);
    outl((uint16_t)(g_rtl.io + RTL_TX_ADDRESS + slot * 4U),
         (uint32_t)(uintptr_t)g_rtl.tx_buffer[slot]);
    /* Umbral FIFO 256 bytes (8 unidades de 32) y longitud de trama. */
    outl((uint16_t)(g_rtl.io + RTL_TX_STATUS + slot * 4U),
         (8U << 11) | transmit_length);
    g_rtl.tx_slot = (uint8_t)((slot + 1U) & 3U);
    return true;
}

static bool rtl_link_up(void) {
    return (in16((uint16_t)(g_rtl.io + RTL_BMSR)) & 0x0004U) != 0U;
}

static const net_device_ops_t g_net_ops = {
    "eth0-rtl8139", g_rtl.mac, 1500U, rtl_send, rtl_link_up,
    rtl_receive_poll
};

static void rtl_release(void) {
    if (g_rtl.rx_ring) kfree(g_rtl.rx_ring);
    for (uint8_t i = 0; i < RTL_TX_SLOTS; i++)
        if (g_rtl.tx_buffer[i]) kfree(g_rtl.tx_buffer[i]);
    g_rtl.rx_ring = NULL;
}

static bool rtl_init(void) {
    const pci_device_t *device = NULL;
    kmemset(&g_rtl, 0, sizeof(g_rtl));
    for (uint32_t i = 0; i < pci_device_count(); i++) {
        const pci_device_t *candidate = pci_device_at(i);
        if (candidate && rtl_id_supported(candidate->vendor_id,
                                          candidate->device_id)) {
            device = candidate;
            break;
        }
    }
    if (!device || !(device->bars[0] & 1U)) return false;
    g_rtl.pci = device;
    g_rtl.io = (uint16_t)(device->bars[0] & ~3U);
    if (!pci_enable_command(device, PCI_COMMAND_IO | PCI_COMMAND_BUSMASTER) ||
        !rtl_reset()) return false;
    for (uint8_t i = 0; i < 6; i++)
        g_rtl.mac[i] = inb((uint16_t)(g_rtl.io + RTL_MAC0 + i));
    if ((g_rtl.mac[0] & 1U) || !(g_rtl.mac[0] | g_rtl.mac[1] |
        g_rtl.mac[2] | g_rtl.mac[3] | g_rtl.mac[4] | g_rtl.mac[5])) return false;
    g_rtl.rx_ring = (uint8_t *)kzalloc(RTL_RX_ALLOC_SIZE);
    for (uint8_t i = 0; i < RTL_TX_SLOTS; i++)
        g_rtl.tx_buffer[i] = (uint8_t *)kmalloc(RTL_TX_BUFFER_SIZE);
    if (!g_rtl.rx_ring || !g_rtl.tx_buffer[0] || !g_rtl.tx_buffer[1] ||
        !g_rtl.tx_buffer[2] || !g_rtl.tx_buffer[3]) {
        rtl_release();
        return false;
    }
    outl((uint16_t)(g_rtl.io + RTL_RX_BUFFER),
         (uint32_t)(uintptr_t)g_rtl.rx_ring);
    out16((uint16_t)(g_rtl.io + RTL_IMR), 0); /* sondeo: sin IRQ compartida */
    out16((uint16_t)(g_rtl.io + RTL_ISR), 0xFFFFU);
    outl((uint16_t)(g_rtl.io + RTL_RCR),
         (4U << 13) | (4U << 8) | (1U << 7) | 0x0EU);
    out16((uint16_t)(g_rtl.io + RTL_CAPR), 0xFFF0U);
    outb((uint16_t)(g_rtl.io + RTL_COMMAND),
         RTL_CMD_RX_ENABLE | RTL_CMD_TX_ENABLE);
    g_rtl.running = true;
    if (!netdev_register(&g_net_ops)) {
        g_rtl.running = false;
        rtl_release();
        return false;
    }
    if (task_create("rtl8139-rx", rtl_poll_task, NULL) < 0) {
        netdev_unregister(&g_net_ops);
        g_rtl.running = false;
        rtl_release();
        return false;
    }
    kprintf("  [RTL8139] PCI %x:%x IO=0x%x MAC=%x:%x:%x:%x:%x:%x polling\n",
            device->vendor_id, device->device_id, g_rtl.io,
            g_rtl.mac[0], g_rtl.mac[1], g_rtl.mac[2],
            g_rtl.mac[3], g_rtl.mac[4], g_rtl.mac[5]);
    return true;
}

static void rtl_shutdown(void) {
    if (!g_rtl.running) return;
    g_rtl.running = false;
    netdev_unregister(&g_net_ops);
    outb((uint16_t)(g_rtl.io + RTL_COMMAND), 0);
    rtl_release();
}

const bk_driver_module_t *bleskernos_driver_query(void) {
    static const bk_driver_module_t module = {
        BK_DRIVER_ABI_VERSION, sizeof(bk_driver_module_t), "rtl8139",
        "Realtek RTL8129/RTL8139 Fast Ethernet", rtl_init, rtl_shutdown
    };
    return &module;
}
