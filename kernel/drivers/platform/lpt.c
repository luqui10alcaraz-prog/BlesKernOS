#include "../../include/lpt.h"
#include "../../include/pic.h"
#include "../../include/pit.h"
#include "../../include/task.h"
#include "../../stdio.h"

#define LPT_DATA_OFFSET    0U
#define LPT_STATUS_OFFSET  1U
#define LPT_CONTROL_OFFSET 2U

#define LPT_STATUS_NOT_BUSY  0x80U
#define LPT_STATUS_ACK       0x40U
#define LPT_STATUS_PAPER_OUT 0x20U
#define LPT_STATUS_SELECTED  0x10U
#define LPT_STATUS_NOT_ERROR 0x08U

#define LPT_CONTROL_STROBE 0x01U
#define LPT_CONTROL_INIT   0x04U
#define LPT_CONTROL_SELECT 0x08U
#define LPT_CONTROL_IDLE   (LPT_CONTROL_INIT | LPT_CONTROL_SELECT)

#define LPT_WRITE_MAX (16U * 1024U * 1024U)

static const uint16_t g_lpt_bases[LPT_MAX_PORTS] = {0x378U, 0x278U, 0x3BCU};
static lpt_port_info_t g_lpt_ports[LPT_MAX_PORTS];
static volatile bool g_lpt_locked;
static bool g_lpt_initialized;
static const lpt_virtual_provider_t *g_virtual[LPT_MAX_VIRTUAL_PROVIDERS];
static uint32_t g_virtual_count;

static uint32_t lpt_timeout_ticks(uint32_t milliseconds) {
    uint32_t hz = pit_get_frequency_hz();
    uint64_t ticks;
    if (!hz) hz = 100U;
    if (!milliseconds) milliseconds = 3000U;
    ticks = ((uint64_t)milliseconds * hz + 999U) / 1000U;
    if (!ticks) ticks = 1U;
    if (ticks > 0x7FFFFFFFU) ticks = 0x7FFFFFFFU;
    return (uint32_t)ticks;
}

static bool lpt_elapsed(uint32_t start, uint32_t duration) {
    return (uint32_t)(pit_get_ticks() - start) >= duration;
}

static void lpt_decode(uint16_t base, uint8_t status,
                       lpt_port_info_t *info) {
    if (!info) return;
    info->base = base;
    info->virtual_port = false;
    info->raw_status = status;
    /* Un bus ISA flotante normalmente devuelve 0xFF. No se escriben patrones
       de prueba porque una impresora real podria interpretarlos como datos. */
    info->present = status != 0xFFU;
    info->busy = (status & LPT_STATUS_NOT_BUSY) == 0U;
    info->selected = (status & LPT_STATUS_SELECTED) != 0U;
    info->paper_out = (status & LPT_STATUS_PAPER_OUT) != 0U;
    info->error = (status & LPT_STATUS_NOT_ERROR) == 0U;
    info->acknowledged = (status & LPT_STATUS_ACK) == 0U;
}

static bool lpt_ready(const lpt_port_info_t *info) {
    return info && info->present && !info->busy && info->selected &&
           !info->paper_out && !info->error;
}

static bool lpt_lock(uint32_t timeout_ticks) {
    uint32_t start = pit_get_ticks();
    for (;;) {
        bool acquired = false;
        task_preempt_disable();
        if (!g_lpt_locked) {
            g_lpt_locked = true;
            acquired = true;
        }
        task_preempt_enable();
        if (acquired) return true;
        if (lpt_elapsed(start, timeout_ticks)) return false;
        task_yield();
    }
}

static void lpt_unlock(void) {
    task_preempt_disable();
    g_lpt_locked = false;
    task_preempt_enable();
}

void lpt_init(void) {
    uint32_t detected = 0;
    if (g_lpt_initialized) return;
    g_lpt_initialized = true;
    for (uint32_t i = 0; i < LPT_MAX_PORTS; i++) {
        uint16_t base = g_lpt_bases[i];
        uint8_t status = inb((uint16_t)(base + LPT_STATUS_OFFSET));
        lpt_decode(base, status, &g_lpt_ports[i]);
        g_lpt_ports[i].name[0] = 'L';
        g_lpt_ports[i].name[1] = 'P';
        g_lpt_ports[i].name[2] = 'T';
        g_lpt_ports[i].name[3] = (char)('1' + i);
        g_lpt_ports[i].name[4] = '\0';
        if (!g_lpt_ports[i].present) continue;
        outb((uint16_t)(base + LPT_CONTROL_OFFSET), LPT_CONTROL_IDLE);
        detected++;
        kprintf("  [LPT] LPT%u base=0x%04X status=0x%02X%s\n",
                i + 1U, base, status,
                lpt_ready(&g_lpt_ports[i]) ? " listo" : "");
    }
    if (!detected) kprintf("  [LPT] no se detectaron puertos paralelos\n");
}

bool lpt_register_virtual_provider(const lpt_virtual_provider_t *provider) {
    if (!provider || !provider->count || !provider->info || !provider->write ||
        g_virtual_count >= LPT_MAX_VIRTUAL_PROVIDERS) return false;
    for (uint32_t i = 0; i < g_virtual_count; i++)
        if (g_virtual[i] == provider) return true;
    g_virtual[g_virtual_count++] = provider;
    return true;
}

bool lpt_unregister_virtual_provider(const lpt_virtual_provider_t *provider) {
    if (!provider) return false;
    for (uint32_t i = 0; i < g_virtual_count; i++) {
        if (g_virtual[i] != provider) continue;
        for (uint32_t j = i + 1U; j < g_virtual_count; j++)
            g_virtual[j - 1U] = g_virtual[j];
        g_virtual[--g_virtual_count] = NULL;
        return true;
    }
    return false;
}

static uint32_t lpt_physical_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < LPT_MAX_PORTS; i++)
        if (g_lpt_ports[i].present) count++;
    return count;
}

uint32_t lpt_port_count(void) {
    uint32_t count;
    if (!g_lpt_initialized) lpt_init();
    count = lpt_physical_count();
    for (uint32_t i = 0; i < g_virtual_count; i++)
        count += g_virtual[i]->count();
    return count;
}

bool lpt_port_info(uint32_t index, lpt_port_info_t *info) {
    uint32_t seen = 0;
    if (!info) return false;
    if (!g_lpt_initialized) lpt_init();
    for (uint32_t i = 0; i < LPT_MAX_PORTS; i++) {
        uint8_t status;
        if (!g_lpt_ports[i].present) continue;
        if (seen++ != index) continue;
        status = inb((uint16_t)(g_lpt_ports[i].base + LPT_STATUS_OFFSET));
        lpt_decode(g_lpt_ports[i].base, status, &g_lpt_ports[i]);
        g_lpt_ports[i].name[0] = 'L'; g_lpt_ports[i].name[1] = 'P';
        g_lpt_ports[i].name[2] = 'T'; g_lpt_ports[i].name[3] = (char)('1' + i);
        g_lpt_ports[i].name[4] = '\0';
        *info = g_lpt_ports[i];
        return true;
    }
    index -= seen;
    for (uint32_t i = 0; i < g_virtual_count; i++) {
        uint32_t count = g_virtual[i]->count();
        if (index < count) return g_virtual[i]->info(index, info);
        index -= count;
    }
    return false;
}

int32_t lpt_write(uint32_t index, const void *data, uint32_t length,
                  uint32_t idle_timeout_ms) {
    const uint8_t *bytes = (const uint8_t *)data;
    lpt_port_info_t port;
    uint32_t timeout;
    uint32_t last_progress;
    uint8_t control = LPT_CONTROL_IDLE;

    if ((!data && length) || length > LPT_WRITE_MAX) return -1;
    if (!length) return 0;
    if (!g_lpt_initialized) lpt_init();
    {
        uint32_t physical = lpt_physical_count();
        if (index >= physical) {
            uint32_t virtual_index = index - physical;
            for (uint32_t i = 0; i < g_virtual_count; i++) {
                uint32_t count = g_virtual[i]->count();
                if (virtual_index < count)
                    return g_virtual[i]->write(virtual_index, data, length,
                                               idle_timeout_ms);
                virtual_index -= count;
            }
            return -2;
        }
    }
    if (!lpt_port_info(index, &port)) return -2;
    timeout = lpt_timeout_ticks(idle_timeout_ms);
    if (!lpt_lock(timeout)) return -3;
    last_progress = pit_get_ticks();

    for (uint32_t offset = 0; offset < length; offset++) {
        for (;;) {
            uint8_t status = inb((uint16_t)(port.base + LPT_STATUS_OFFSET));
            lpt_decode(port.base, status, &port);
            if (lpt_ready(&port)) break;
            if (!port.present || port.paper_out || port.error ||
                !port.selected) {
                lpt_unlock();
                return -4;
            }
            if (lpt_elapsed(last_progress, timeout)) {
                lpt_unlock();
                return -5;
            }
            task_yield();
        }
        outb((uint16_t)(port.base + LPT_DATA_OFFSET), bytes[offset]);
        io_wait();
        /* SPP: presenta el byte, pulsa STROBE y vuelve a reposo. */
        outb((uint16_t)(port.base + LPT_CONTROL_OFFSET),
             (uint8_t)(control | LPT_CONTROL_STROBE));
        io_wait();
        outb((uint16_t)(port.base + LPT_CONTROL_OFFSET), control);
        last_progress = pit_get_ticks();
        if ((offset & 0xFFU) == 0xFFU) task_yield();
    }
    lpt_unlock();
    return (int32_t)length;
}
