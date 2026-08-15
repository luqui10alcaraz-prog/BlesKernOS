#include "include/smp_watchdog.h"
#include "include/smp.h"
#include "include/pit.h"
#include "include/vga.h"
#include "include/memory.h"

#define WATCHDOG_SECONDS 5U

static volatile uint32_t g_heartbeat[SMP_MAX_CPUS];
static uint32_t g_observed[SMP_MAX_CPUS];
static uint32_t g_last_progress[SMP_MAX_CPUS];
static uint8_t g_reported[SMP_MAX_CPUS];
static uint32_t g_last_poll;
static volatile uint32_t g_poll_lock;
static volatile uint32_t g_stalled_mask;
static volatile uint32_t g_active;

static bool watchdog_try_enter(void) {
    uint32_t value = 1U;
    __asm__ volatile ("xchgl %0, %1"
                      : "+r"(value), "+m"(g_poll_lock) : :
                        "memory", "cc");
    return value == 0U;
}

static void watchdog_leave(void) {
    __asm__ volatile ("" : : : "memory");
    g_poll_lock = 0U;
}

void smp_watchdog_init(void) {
    uint32_t now = pit_get_ticks();
    kmemset((void *)g_heartbeat, 0, sizeof(g_heartbeat));
    kmemset(g_observed, 0, sizeof(g_observed));
    kmemset(g_reported, 0, sizeof(g_reported));
    for (uint32_t cpu = 0U; cpu < SMP_MAX_CPUS; cpu++)
        g_last_progress[cpu] = now;
    g_last_poll = now;
    g_poll_lock = 0U;
    g_stalled_mask = 0U;
    g_active = 0U;
}

void smp_watchdog_heartbeat(void) {
    uint32_t cpu = smp_cpu_index();
    if (cpu >= SMP_MAX_CPUS) cpu = 0U;
    /* Cada CPU es el unico escritor de su propia celda; una escritura de 32
       bits alineada ya es atomica en x86. El LOCK global sólo ensuciaba el bus. */
    g_heartbeat[cpu]++;
    __asm__ volatile ("" : : : "memory");
}

void smp_watchdog_poll(void) {
    uint32_t now;
    uint32_t hz;
    uint32_t online;
    uint32_t timeout;

    if (smp_cpu_index() != 0U) return;
    now = pit_get_ticks();
    hz = pit_get_frequency_hz();
    if (!hz) hz = 100U;

    /* APs are intentionally parked between "CPU online" and release of the
     * per-CPU scheduler. They cannot produce scheduler heartbeats yet, so
     * reporting them as stalled during boot is a false positive. */
    if (!smp_scheduler_started()) {
        g_last_poll = now;
        g_active = 0U;
        g_stalled_mask = 0U;
        return;
    }
    if (!g_active) {
        if (!watchdog_try_enter()) return;
        if (!g_active) {
            uint32_t count = smp_cpu_count();
            if (!count) count = 1U;
            if (count > SMP_MAX_CPUS) count = SMP_MAX_CPUS;
            for (uint32_t cpu = 0U; cpu < count; cpu++) {
                g_observed[cpu] = g_heartbeat[cpu];
                g_last_progress[cpu] = now;
                g_reported[cpu] = 0U;
            }
            g_stalled_mask = 0U;
            g_last_poll = now;
            g_active = 1U;
        }
        watchdog_leave();
        return;
    }
    if ((uint32_t)(now - g_last_poll) < hz) return;
    if (!watchdog_try_enter()) return;
    now = pit_get_ticks();
    if ((uint32_t)(now - g_last_poll) < hz) {
        watchdog_leave();
        return;
    }
    g_last_poll = now;
    timeout = hz * WATCHDOG_SECONDS;
    online = smp_cpu_count();
    if (!online) online = 1U;
    if (online > SMP_MAX_CPUS) online = SMP_MAX_CPUS;

    for (uint32_t cpu = 0U; cpu < online; cpu++) {
        if (!smp_cpu_online(cpu)) continue;
        uint32_t heartbeat = g_heartbeat[cpu];
        uint32_t bit = 1U << cpu;
        if (heartbeat != g_observed[cpu]) {
            g_observed[cpu] = heartbeat;
            g_last_progress[cpu] = now;
            g_reported[cpu] = 0U;
            g_stalled_mask &= ~bit;
            continue;
        }
        if ((uint32_t)(now - g_last_progress[cpu]) < timeout) continue;
        g_stalled_mask |= bit;
        if (!g_reported[cpu]) {
            g_reported[cpu] = 1U;
            kprintf("[WATCHDOG] CPU%u sin progreso durante %u s "
                    "heartbeat=%u detectado_por=CPU%u\n",
                    cpu, WATCHDOG_SECONDS, heartbeat, smp_cpu_index());
        }
    }
    watchdog_leave();
}

uint32_t smp_watchdog_stalled_mask(void) { return g_stalled_mask; }
