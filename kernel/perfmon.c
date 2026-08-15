#include "include/perfmon.h"
#include "include/pit.h"
#include "include/pic.h"
#include "include/vga.h"
#include "include/task.h"
#include "include/memory.h"
#include "include/gfx.h"
#include "include/block.h"
#include "include/vfs.h"
#include "../gui/gui.h"

#ifndef PERFMON_DEFAULT_INTERVAL_SECONDS
#define PERFMON_DEFAULT_INTERVAL_SECONDS 10U
#endif

#ifndef PERFMON_DEFAULT_ENABLED
#define PERFMON_DEFAULT_ENABLED 0
#endif

#define PERFMON_MIN_INTERVAL_SECONDS 10U
#define PERFMON_MAX_INTERVAL_SECONDS 600U
#define PERFMON_TOP_TASKS 3U

typedef struct {
    uint32_t calls;
    uint64_t total_cycles;
    uint64_t max_cycles;
} perf_scope_stat_t;

typedef struct {
    uint32_t calls;
    uint32_t sectors;
    uint32_t failures;
    uint64_t total_cycles;
    uint64_t max_cycles;
} perf_io_stat_t;

typedef struct {
    perf_scope_stat_t scopes[PERF_SCOPE_COUNT];
    uint32_t irq[16];
    uint32_t scheduler_ticks;
    uint32_t scheduler_busy_ticks;
    uint32_t scheduler_switches;
    uint32_t scheduler_fast_quantum;
    uint32_t scheduler_preempt_blocked;
    uint32_t gui_loops;
    uint32_t gui_events;
    uint32_t gui_mouse_moves;
    uint32_t gui_frames;
    uint32_t gui_content_frames;
    uint32_t gui_full_frames;
    uint32_t gui_cursor_only_frames;
    uint32_t gui_hw_cursor_frames;
    uint32_t gui_gpu_frames;
    uint64_t gui_dirty_pixels;
    uint32_t gui_screen_pixels;
    perf_io_stat_t io[PERFMON_IO_TYPE_COUNT][2];
} perf_interval_t;

typedef struct {
    uint32_t pid;
    uint32_t ticks;
} perf_task_baseline_t;

typedef struct {
    uint32_t pid;
    uint32_t ticks;
    uint32_t delta;
    uint32_t memory_bytes;
    task_state_t state;
    bool idle;
    char name[TASK_NAME_LEN];
} perf_task_snapshot_t;

static volatile bool g_enabled;
static volatile bool g_force_report;
static volatile bool g_reporting;
static bool g_tsc_supported;
static uint32_t g_interval_seconds = PERFMON_DEFAULT_INTERVAL_SECONDS;
static uint32_t g_last_report_tick;
static uint64_t g_last_report_tsc;
static perf_interval_t g_stats;
static perf_task_baseline_t g_task_baseline[TASK_MAX];
static uint32_t g_task_baseline_count;

static uint32_t perfmon_irq_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void perfmon_irq_restore(uint32_t flags) {
    if (flags & (1U << 9)) __asm__ volatile ("sti" : : : "memory");
}

static bool perfmon_cpu_has_cpuid(void) {
    uint32_t before;
    uint32_t after;
    uint32_t toggled;

    __asm__ volatile ("pushfl; popl %0" : "=r"(before));
    toggled = before ^ (1U << 21);
    __asm__ volatile ("pushl %0; popfl" : : "r"(toggled) : "cc", "memory");
    __asm__ volatile ("pushfl; popl %0" : "=r"(after));
    __asm__ volatile ("pushl %0; popfl" : : "r"(before) : "cc", "memory");
    return ((before ^ after) & (1U << 21)) != 0U;
}

static bool perfmon_cpu_has_tsc(void) {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    if (!perfmon_cpu_has_cpuid()) return false;
    eax = 0U;
    __asm__ volatile ("cpuid"
                      : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < 1U) return false;
    eax = 1U;
    __asm__ volatile ("cpuid"
                      : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx & (1U << 4)) != 0U;
}

static uint64_t perfmon_read_tsc(void) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

static uint32_t perfmon_u64_to_u32(uint64_t value) {
    return value > 0xFFFFFFFFULL ? 0xFFFFFFFFU : (uint32_t)value;
}

static uint32_t perfmon_cycles_to_us(uint64_t cycles,
                                     uint32_t cycles_per_us) {
    if (!cycles_per_us) return 0U;
    return perfmon_u64_to_u32(cycles / cycles_per_us);
}

static uint32_t perfmon_scope_average_us(const perf_scope_stat_t *stat,
                                         uint32_t cycles_per_us) {
    if (!stat || !stat->calls || !cycles_per_us) return 0U;
    return perfmon_cycles_to_us(stat->total_cycles / stat->calls,
                                cycles_per_us);
}

static uint32_t perfmon_scope_share(const perf_scope_stat_t *stat,
                                    uint64_t elapsed_cycles) {
    if (!stat || !elapsed_cycles) return 0U;
    return perfmon_u64_to_u32((stat->total_cycles * 100ULL) /
                              elapsed_cycles);
}

static void perfmon_capture_task_baseline(void) {
    uint32_t count;

    task_preempt_disable();
    count = task_count();
    if (count > TASK_MAX) count = TASK_MAX;
    g_task_baseline_count = 0U;
    for (uint32_t i = 0; i < count; i++) {
        const task_t *task = task_get(i);
        if (!task) continue;
        g_task_baseline[g_task_baseline_count].pid = task->pid;
        g_task_baseline[g_task_baseline_count].ticks = task->cpu_ticks;
        g_task_baseline_count++;
    }
    task_preempt_enable();
}

static uint32_t perfmon_previous_task_ticks(uint32_t pid, bool *found) {
    for (uint32_t i = 0; i < g_task_baseline_count; i++) {
        if (g_task_baseline[i].pid == pid) {
            if (found) *found = true;
            return g_task_baseline[i].ticks;
        }
    }
    if (found) *found = false;
    return 0U;
}

static uint32_t perfmon_capture_tasks(perf_task_snapshot_t *tasks,
                                      uint32_t capacity) {
    uint32_t count;
    uint32_t copied = 0U;

    if (!tasks || !capacity) return 0U;
    task_preempt_disable();
    count = task_count();
    if (count > capacity) count = capacity;
    for (uint32_t i = 0; i < count; i++) {
        const task_t *task = task_get(i);
        bool found;
        uint32_t previous;

        if (!task) continue;
        previous = perfmon_previous_task_ticks(task->pid, &found);
        tasks[copied].pid = task->pid;
        tasks[copied].ticks = task->cpu_ticks;
        tasks[copied].delta = found ? task->cpu_ticks - previous : 0U;
        tasks[copied].memory_bytes = task->memory_bytes;
        tasks[copied].state = task->state;
        tasks[copied].idle = task->idle;
        kstrncpy(tasks[copied].name, task->name, TASK_NAME_LEN - 1U);
        tasks[copied].name[TASK_NAME_LEN - 1U] = '\0';
        copied++;
    }

    g_task_baseline_count = copied;
    for (uint32_t i = 0; i < copied; i++) {
        g_task_baseline[i].pid = tasks[i].pid;
        g_task_baseline[i].ticks = tasks[i].ticks;
    }
    task_preempt_enable();
    return copied;
}

static char perfmon_task_state_char(task_state_t state) {
    if (state == TASK_RUNNING) return 'R';
    if (state == TASK_READY) return 'r';
    if (state == TASK_SLEEPING) return 'S';
    if (state == TASK_ZOMBIE) return 'Z';
    return '-';
}

static void perfmon_sort_tasks(perf_task_snapshot_t *tasks, uint32_t count) {
    for (uint32_t i = 0; i + 1U < count; i++) {
        uint32_t best = i;
        for (uint32_t j = i + 1U; j < count; j++) {
            if ((!tasks[j].idle && tasks[best].idle) ||
                (tasks[j].idle == tasks[best].idle &&
                 tasks[j].delta > tasks[best].delta))
                best = j;
        }
        if (best != i) {
            perf_task_snapshot_t temporary = tasks[i];
            tasks[i] = tasks[best];
            tasks[best] = temporary;
        }
    }
}

void perfmon_init(void) {
    g_tsc_supported = perfmon_cpu_has_tsc();
    g_enabled = PERFMON_DEFAULT_ENABLED ? true : false;
    g_force_report = false;
    g_reporting = false;
    /*
     * El perfilador queda apagado por defecto: medir cada IRQ, cambio de tarea
     * y frame tiene un coste visible en Pentium II/III. "perfmon on" y los
     * benchmarks pueden habilitarlo cuando se necesita diagnosticar.
     */
    perfmon_reset();
}

bool perfmon_enabled(void) {
    return g_enabled;
}

void perfmon_set_enabled(bool enabled) {
    g_enabled = enabled;
    if (enabled) perfmon_reset();
}

void perfmon_reset(void) {
    uint32_t flags = perfmon_irq_save();
    kmemset(&g_stats, 0, sizeof(g_stats));
    g_last_report_tick = pit_get_ticks();
    g_last_report_tsc = g_tsc_supported ? perfmon_read_tsc() : 0ULL;
    g_force_report = false;
    perfmon_irq_restore(flags);
    perfmon_capture_task_baseline();
}

void perfmon_force_report(void) {
    g_force_report = true;
}

void perfmon_set_interval_seconds(uint32_t seconds) {
    if (seconds < PERFMON_MIN_INTERVAL_SECONDS)
        seconds = PERFMON_MIN_INTERVAL_SECONDS;
    if (seconds > PERFMON_MAX_INTERVAL_SECONDS)
        seconds = PERFMON_MAX_INTERVAL_SECONDS;
    g_interval_seconds = seconds;
}

uint32_t perfmon_interval_seconds(void) {
    return g_interval_seconds;
}

uint64_t perfmon_scope_begin(void) {
    if (!g_enabled || g_reporting || !g_tsc_supported) return 0ULL;
    return perfmon_read_tsc();
}

void perfmon_scope_end(perf_scope_id_t scope, uint64_t started) {
    perf_scope_stat_t *stat;
    uint64_t elapsed;

    if (!g_enabled || g_reporting || !g_tsc_supported || !started ||
        (uint32_t)scope >= PERF_SCOPE_COUNT) return;
    elapsed = perfmon_read_tsc() - started;
    stat = &g_stats.scopes[scope];
    stat->calls++;
    stat->total_cycles += elapsed;
    if (elapsed > stat->max_cycles) stat->max_cycles = elapsed;
}

void perfmon_irq(uint8_t irq) {
    if (g_enabled && !g_reporting && irq < 16U) g_stats.irq[irq]++;
}

void perfmon_scheduler_tick(bool busy) {
    if (!g_enabled || g_reporting) return;
    g_stats.scheduler_ticks++;
    if (busy) g_stats.scheduler_busy_ticks++;
}

void perfmon_scheduler_switch(void) {
    if (g_enabled && !g_reporting) g_stats.scheduler_switches++;
}

void perfmon_scheduler_fast_quantum(void) {
    if (g_enabled && !g_reporting) g_stats.scheduler_fast_quantum++;
}

void perfmon_scheduler_preempt_blocked(void) {
    if (g_enabled && !g_reporting) g_stats.scheduler_preempt_blocked++;
}

void perfmon_gui_loop(void) {
    if (g_enabled && !g_reporting) g_stats.gui_loops++;
}

void perfmon_gui_event(bool mouse_move) {
    if (!g_enabled || g_reporting) return;
    g_stats.gui_events++;
    if (mouse_move) g_stats.gui_mouse_moves++;
}

void perfmon_gui_frame(uint32_t dirty_pixels, uint32_t screen_pixels,
                       bool content_dirty, bool hardware_cursor,
                       bool gpu_presented) {
    if (!g_enabled || g_reporting) return;
    g_stats.gui_frames++;
    if (content_dirty) g_stats.gui_content_frames++;
    else g_stats.gui_cursor_only_frames++;
    if (screen_pixels) {
        if (dirty_pixels > screen_pixels) dirty_pixels = screen_pixels;
        g_stats.gui_screen_pixels = screen_pixels;
        if (dirty_pixels == screen_pixels) g_stats.gui_full_frames++;
    }
    g_stats.gui_dirty_pixels += dirty_pixels;
    if (hardware_cursor) g_stats.gui_hw_cursor_frames++;
    if (gpu_presented) g_stats.gui_gpu_frames++;
}

void perfmon_block_complete(uint32_t device_type, bool write,
                            uint8_t sectors, bool success,
                            uint64_t started) {
    perf_io_stat_t *io;
    uint64_t elapsed = 0ULL;
    perf_scope_id_t scope = write ? PERF_SCOPE_BLOCK_WRITE
                                  : PERF_SCOPE_BLOCK_READ;

    if (!g_enabled || g_reporting) return;
    if (g_tsc_supported && started) elapsed = perfmon_read_tsc() - started;
    perfmon_scope_end(scope, started);
    if (device_type >= PERFMON_IO_TYPE_COUNT) device_type = 0U;
    io = &g_stats.io[device_type][write ? 1U : 0U];
    io->calls++;
    io->sectors += sectors;
    if (!success) io->failures++;
    io->total_cycles += elapsed;
    if (elapsed > io->max_cycles) io->max_cycles = elapsed;
}

static void perfmon_print_io(const perf_interval_t *snapshot,
                             uint32_t cycles_per_us) {
    for (uint32_t type = 1U; type < PERFMON_IO_TYPE_COUNT; type++) {
        const perf_io_stat_t *read = &snapshot->io[type][0];
        const perf_io_stat_t *write = &snapshot->io[type][1];
        uint32_t read_average = read->calls && cycles_per_us
            ? perfmon_cycles_to_us(read->total_cycles / read->calls,
                                    cycles_per_us) : 0U;
        uint32_t write_average = write->calls && cycles_per_us
            ? perfmon_cycles_to_us(write->total_cycles / write->calls,
                                    cycles_per_us) : 0U;
        if (!read->calls && !write->calls) continue;
        kprintf("[PERF:IO] %s R=%u/%usec avg=%uus max=%uus err=%u "
                "W=%u/%usec avg=%uus max=%uus err=%u\n",
                block_type_name((block_device_type_t)type),
                read->calls, read->sectors, read_average,
                perfmon_cycles_to_us(read->max_cycles, cycles_per_us),
                read->failures,
                write->calls, write->sectors, write_average,
                perfmon_cycles_to_us(write->max_cycles, cycles_per_us),
                write->failures);
    }
}

static uint32_t perfmon_max_io_us(const perf_interval_t *snapshot,
                                  uint32_t cycles_per_us) {
    uint64_t maximum = 0ULL;
    for (uint32_t type = 0U; type < PERFMON_IO_TYPE_COUNT; type++) {
        for (uint32_t direction = 0U; direction < 2U; direction++) {
            if (snapshot->io[type][direction].max_cycles > maximum)
                maximum = snapshot->io[type][direction].max_cycles;
        }
    }
    return perfmon_cycles_to_us(maximum, cycles_per_us);
}

static void perfmon_copy_text(char *destination, uint32_t capacity,
                              const char *source) {
    if (!destination || !capacity) return;
    kstrncpy(destination, source ? source : "", capacity - 1U);
    destination[capacity - 1U] = '\0';
}

bool perfmon_capture_snapshot(perfmon_snapshot_t *output,
                              bool reset_interval) {
    perf_interval_t snapshot;
    system_memory_info_t memory;
    heap_info_t heap;
    const gfx_info_t *gfx;
    uint32_t flags;
    uint32_t now;
    uint32_t hz;
    uint32_t elapsed_ticks;
    uint64_t now_tsc;
    uint64_t elapsed_cycles;
    uint64_t elapsed_us;
    uint32_t cycles_per_us = 0U;

    if (!output || g_reporting) return false;
    g_reporting = true;
    now = pit_get_ticks();
    hz = pit_get_frequency_hz();
    if (!hz) hz = 1U;
    now_tsc = g_tsc_supported ? perfmon_read_tsc() : 0ULL;

    flags = perfmon_irq_save();
    elapsed_ticks = now - g_last_report_tick;
    if (!elapsed_ticks) elapsed_ticks = 1U;
    snapshot = g_stats;
    elapsed_cycles = g_tsc_supported ? now_tsc - g_last_report_tsc : 0ULL;
    if (reset_interval) {
        kmemset(&g_stats, 0, sizeof(g_stats));
        g_last_report_tick = now;
        g_last_report_tsc = now_tsc;
        g_force_report = false;
    }
    perfmon_irq_restore(flags);

    kmemset(output, 0, sizeof(*output));
    output->struct_size = sizeof(*output);
    output->enabled = g_enabled ? 1U : 0U;
    output->tsc_supported = g_tsc_supported ? 1U : 0U;
    output->elapsed_ms = (uint32_t)(((uint64_t)elapsed_ticks * 1000ULL) / hz);
    if (!output->elapsed_ms) output->elapsed_ms = 1U;

    elapsed_us = ((uint64_t)elapsed_ticks * 1000000ULL) / hz;
    if (elapsed_us && elapsed_cycles)
        cycles_per_us = perfmon_u64_to_u32(elapsed_cycles / elapsed_us);
    output->tsc_mhz = cycles_per_us;
    output->cpu_percent = snapshot.scheduler_ticks
        ? (snapshot.scheduler_busy_ticks * 100U) / snapshot.scheduler_ticks : 0U;

    mm_get_system_info(&memory);
    mm_get_info(&heap);
    output->memory_total_bytes = perfmon_u64_to_u32(memory.total_bytes);
    output->memory_used_bytes = perfmon_u64_to_u32(memory.used_bytes);
    output->memory_free_bytes = perfmon_u64_to_u32(memory.free_bytes);
    output->heap_total_blocks = heap.total_blocks;
    output->heap_used_blocks = heap.used_blocks;
    output->heap_free_blocks = heap.free_blocks;

    gfx = gfx_get_info();
    output->gfx_width = gfx ? gfx->width : 0U;
    output->gfx_height = gfx ? gfx->height : 0U;
    output->gfx_bpp = gfx ? gfx->bpp : 0U;
    output->gfx_capabilities = gfx_driver_capabilities();
    perfmon_copy_text(output->gfx_driver, sizeof(output->gfx_driver),
                      gfx_driver_name());

    output->gui_loops = snapshot.gui_loops;
    output->gui_events = snapshot.gui_events;
    output->gui_mouse_moves = snapshot.gui_mouse_moves;
    output->gui_frames = snapshot.gui_frames;
    output->gui_fps = (uint32_t)(((uint64_t)snapshot.gui_frames * hz) /
                                 elapsed_ticks);
    output->gui_content_frames = snapshot.gui_content_frames;
    output->gui_full_frames = snapshot.gui_full_frames;
    output->gui_cursor_only_frames = snapshot.gui_cursor_only_frames;
    output->gui_hw_cursor_frames = snapshot.gui_hw_cursor_frames;
    output->gui_gpu_frames = snapshot.gui_gpu_frames;
    if (snapshot.gui_content_frames && snapshot.gui_screen_pixels) {
        uint64_t possible = (uint64_t)snapshot.gui_content_frames *
                            snapshot.gui_screen_pixels;
        if (possible)
            output->gui_dirty_percent = perfmon_u64_to_u32(
                (snapshot.gui_dirty_pixels * 100ULL) / possible);
    }

#define PERFMON_COPY_SCOPE(prefix, scope_id) do { \
    const perf_scope_stat_t *scope_stat = &snapshot.scopes[(scope_id)]; \
    output->prefix##_average_us = perfmon_scope_average_us(scope_stat, cycles_per_us); \
    output->prefix##_max_us = perfmon_cycles_to_us(scope_stat->max_cycles, cycles_per_us); \
    output->prefix##_share_percent = perfmon_scope_share(scope_stat, elapsed_cycles); \
} while (0)
    PERFMON_COPY_SCOPE(frame, PERF_SCOPE_GUI_FRAME);
    PERFMON_COPY_SCOPE(compose, PERF_SCOPE_GUI_COMPOSE);
    PERFMON_COPY_SCOPE(present, PERF_SCOPE_GFX_PRESENT);
    PERFMON_COPY_SCOPE(gpu, PERF_SCOPE_GPU_PRESENT);
    PERFMON_COPY_SCOPE(fpu, PERF_SCOPE_FPU_SWITCH);
#undef PERFMON_COPY_SCOPE

    output->scheduler_ticks = snapshot.scheduler_ticks;
    output->scheduler_busy_ticks = snapshot.scheduler_busy_ticks;
    output->scheduler_switches = snapshot.scheduler_switches;
    output->scheduler_fast_quantum = snapshot.scheduler_fast_quantum;
    output->scheduler_preempt_blocked = snapshot.scheduler_preempt_blocked;

    for (uint32_t irq = 0U; irq < PERFMON_IRQ_COUNT; irq++) {
        output->irq[irq] = snapshot.irq[irq];
        output->irq_total += snapshot.irq[irq];
    }
    for (uint32_t type = 0U; type < PERFMON_IO_TYPE_COUNT; type++) {
        const perf_io_stat_t *read = &snapshot.io[type][0];
        const perf_io_stat_t *write = &snapshot.io[type][1];
        perfmon_io_snapshot_t *io = &output->io[type];
        io->read_calls = read->calls;
        io->read_sectors = read->sectors;
        io->read_failures = read->failures;
        io->read_average_us = read->calls && cycles_per_us
            ? perfmon_cycles_to_us(read->total_cycles / read->calls,
                                   cycles_per_us) : 0U;
        io->read_max_us = perfmon_cycles_to_us(read->max_cycles,
                                               cycles_per_us);
        io->write_calls = write->calls;
        io->write_sectors = write->sectors;
        io->write_failures = write->failures;
        io->write_average_us = write->calls && cycles_per_us
            ? perfmon_cycles_to_us(write->total_cycles / write->calls,
                                   cycles_per_us) : 0U;
        io->write_max_us = perfmon_cycles_to_us(write->max_cycles,
                                                cycles_per_us);
    }

    if (reset_interval) perfmon_capture_task_baseline();
    g_reporting = false;
    return true;
}

static void perfmon_print_hints(const perf_interval_t *snapshot,
                                uint32_t present_share,
                                uint32_t gpu_share,
                                uint32_t compose_share,
                                uint32_t fpu_share,
                                uint32_t max_io_us,
                                uint32_t cpu_percent) {
    uint32_t other_irqs = 0U;
    uint32_t video_share = present_share + gpu_share;
    bool printed = false;

    for (uint32_t irq = 1U; irq < 16U; irq++) other_irqs += snapshot->irq[irq];

    if (video_share >= 35U ||
        snapshot->scopes[PERF_SCOPE_GFX_PRESENT].max_cycles >
            snapshot->scopes[PERF_SCOPE_GUI_COMPOSE].max_cycles * 3ULL) {
        kprintf("[PERF:HINT] probable cuello en framebuffer/driver: video=%u%%\n",
                video_share);
        printed = true;
    } else if (compose_share >= 35U) {
        kprintf("[PERF:HINT] probable cuello en dibujo software: compose=%u%%\n",
                compose_share);
        printed = true;
    } else if (max_io_us >= 20000U) {
        kprintf("[PERF:HINT] E/S lenta o espera del controlador: max=%uus\n",
                max_io_us);
        printed = true;
    } else if (fpu_share >= 10U) {
        kprintf("[PERF:HINT] cambios de tarea/x87 costosos: x87=%u%%\n",
                fpu_share);
        printed = true;
    } else if (snapshot->scheduler_ticks &&
               other_irqs > snapshot->scheduler_ticks * 5U) {
        kprintf("[PERF:HINT] posible tormenta de IRQ: no-timer=%u\n",
                other_irqs);
        printed = true;
    } else if (cpu_percent >= 85U) {
        kprintf("[PERF:HINT] CPU saturada; revise PERF:TASK y repintados\n");
        printed = true;
    }

    if (snapshot->gui_frames &&
        snapshot->gui_full_frames * 2U > snapshot->gui_frames) {
        kprintf("[PERF:HINT] demasiados frames completos: %u de %u\n",
                snapshot->gui_full_frames, snapshot->gui_frames);
        printed = true;
    }
    if (!printed)
        kprintf("[PERF:HINT] sin cuello dominante; compare TASK, IRQ e IO\n");
}

void perfmon_poll(void) {
    perf_interval_t snapshot;
    perf_task_snapshot_t tasks[TASK_MAX];
    heap_info_t heap;
    const gfx_info_t *gfx;
    uint32_t flags;
    uint32_t now;
    uint32_t elapsed_ticks;
    uint32_t hz;
    uint64_t now_tsc;
    uint64_t elapsed_cycles;
    uint64_t elapsed_us;
    uint32_t cycles_per_us = 0U;
    uint32_t cpu_percent;
    uint32_t fps;
    uint32_t dirty_percent = 0U;
    uint32_t frame_share;
    uint32_t compose_share;
    uint32_t present_share;
    uint32_t gpu_share;
    uint32_t fpu_share;
    uint32_t max_io_us;
    uint32_t task_count_snapshot;
    uint32_t printed_tasks = 0U;

    /* Nunca emitir reportes automaticos: solo responder a perfmon now. */
    if (!g_enabled || g_reporting || !g_force_report) return;
    now = pit_get_ticks();
    hz = pit_get_frequency_hz();
    if (!hz) hz = 1U;

    g_reporting = true;
    now_tsc = g_tsc_supported ? perfmon_read_tsc() : 0ULL;
    flags = perfmon_irq_save();
    elapsed_ticks = now - g_last_report_tick;
    if (!elapsed_ticks) elapsed_ticks = 1U;
    snapshot = g_stats;
    kmemset(&g_stats, 0, sizeof(g_stats));
    g_last_report_tick = now;
    elapsed_cycles = g_tsc_supported ? now_tsc - g_last_report_tsc : 0ULL;
    g_last_report_tsc = now_tsc;
    g_force_report = false;
    perfmon_irq_restore(flags);

    task_count_snapshot = perfmon_capture_tasks(tasks, TASK_MAX);
    perfmon_sort_tasks(tasks, task_count_snapshot);
    mm_get_info(&heap);
    gfx = gfx_get_info();

    elapsed_us = ((uint64_t)elapsed_ticks * 1000000ULL) / hz;
    if (elapsed_us && elapsed_cycles)
        cycles_per_us = perfmon_u64_to_u32(elapsed_cycles / elapsed_us);
    cpu_percent = snapshot.scheduler_ticks
        ? (snapshot.scheduler_busy_ticks * 100U) / snapshot.scheduler_ticks : 0U;
    fps = (uint32_t)(((uint64_t)snapshot.gui_frames * hz) / elapsed_ticks);
    if (snapshot.gui_content_frames && snapshot.gui_screen_pixels) {
        uint64_t possible = (uint64_t)snapshot.gui_content_frames *
                            snapshot.gui_screen_pixels;
        if (possible)
            dirty_percent = perfmon_u64_to_u32(
                (snapshot.gui_dirty_pixels * 100ULL) / possible);
    }

    frame_share = perfmon_scope_share(
        &snapshot.scopes[PERF_SCOPE_GUI_FRAME], elapsed_cycles);
    compose_share = perfmon_scope_share(
        &snapshot.scopes[PERF_SCOPE_GUI_COMPOSE], elapsed_cycles);
    present_share = perfmon_scope_share(
        &snapshot.scopes[PERF_SCOPE_GFX_PRESENT], elapsed_cycles);
    gpu_share = perfmon_scope_share(
        &snapshot.scopes[PERF_SCOPE_GPU_PRESENT], elapsed_cycles);
    fpu_share = perfmon_scope_share(
        &snapshot.scopes[PERF_SCOPE_FPU_SWITCH], elapsed_cycles);
    max_io_us = perfmon_max_io_us(&snapshot, cycles_per_us);

    /* Reporte detallado solicitado manualmente con "perfmon now". */
    kprintf("[PERF] %us cpu=%u%% tsc=%uMHz gfx=%s %ux%ux%u caps=%x\n",
            elapsed_ticks / hz, cpu_percent, cycles_per_us,
            gfx_driver_name(), gfx ? gfx->width : 0U,
            gfx ? gfx->height : 0U, gfx ? gfx->bpp : 0U,
            gfx_driver_capabilities());
    kprintf("[PERF:GUI] loop=%u ev=%u mouse=%u frame=%u fps=%u full=%u "
            "cursor=%u hwcur=%u gpu=%u dirty=%u%%\n",
            snapshot.gui_loops, snapshot.gui_events,
            snapshot.gui_mouse_moves, snapshot.gui_frames, fps,
            snapshot.gui_full_frames, snapshot.gui_cursor_only_frames,
            snapshot.gui_hw_cursor_frames, snapshot.gui_gpu_frames,
            dirty_percent);
    kprintf("[PERF:TIME] frame avg=%uus max=%uus share=%u%% compose=%u%% "
            "present=%u%% gpu=%u%%\n",
            perfmon_scope_average_us(
                &snapshot.scopes[PERF_SCOPE_GUI_FRAME], cycles_per_us),
            perfmon_cycles_to_us(
                snapshot.scopes[PERF_SCOPE_GUI_FRAME].max_cycles,
                cycles_per_us),
            frame_share, compose_share, present_share, gpu_share);
    kprintf("[PERF:SCHED] tick=%u switch=%u qfast=%u blocked=%u "
            "sched=%u%% x87=%u%% avg=%uus max=%uus\n",
            snapshot.scheduler_ticks, snapshot.scheduler_switches,
            snapshot.scheduler_fast_quantum,
            snapshot.scheduler_preempt_blocked,
            perfmon_scope_share(
                &snapshot.scopes[PERF_SCOPE_SCHEDULER], elapsed_cycles),
            fpu_share,
            perfmon_scope_average_us(
                &snapshot.scopes[PERF_SCOPE_FPU_SWITCH], cycles_per_us),
            perfmon_cycles_to_us(
                snapshot.scopes[PERF_SCOPE_FPU_SWITCH].max_cycles,
                cycles_per_us));
    perfmon_print_io(&snapshot, cycles_per_us);
    kprintf("[PERF:IRQ] 0=%u 1=%u 3=%u 4=%u 6=%u 12=%u 14=%u 15=%u\n",
            snapshot.irq[0], snapshot.irq[1], snapshot.irq[3],
            snapshot.irq[4], snapshot.irq[6], snapshot.irq[12],
            snapshot.irq[14], snapshot.irq[15]);
    kprintf("[PERF:MEM] used=%uK free=%uK blocks=%u tasks=%u\n",
            (uint32_t)(heap.used_bytes / 1024U),
            (uint32_t)(heap.free_bytes / 1024U), heap.used_blocks,
            task_count_snapshot);

    for (uint32_t i = 0; i < task_count_snapshot &&
         printed_tasks < PERFMON_TOP_TASKS; i++) {
        uint32_t percent;
        if (tasks[i].idle || !tasks[i].delta) continue;
        percent = snapshot.scheduler_ticks
            ? (tasks[i].delta * 100U) / snapshot.scheduler_ticks : 0U;
        kprintf("[PERF:TASK] pid=%u %s %c cpu=%u%% ticks=%u mem=%uK\n",
                tasks[i].pid, tasks[i].name,
                perfmon_task_state_char(tasks[i].state), percent,
                tasks[i].delta, tasks[i].memory_bytes / 1024U);
        printed_tasks++;
    }
    perfmon_print_hints(&snapshot, present_share, gpu_share, compose_share,
                        fpu_share, max_io_us, cpu_percent);
    g_reporting = false;
}


/* -------------------------------------------------------------------------
 * Benchmark sintetico de diagnostico
 *
 * No pretende reemplazar un benchmark estandar. Sirve para comparar equipos
 * que ejecutan la misma version de BlesKernOS y separar CPU, RAM, dibujo,
 * presentacion, disco y scheduler. No escribe sectores y restaura la region
 * de pantalla utilizada por la prueba.
 * ------------------------------------------------------------------------- */

typedef struct {
    uint64_t cycles;
    uint32_t ticks;
} perfbench_stamp_t;

static perfbench_stamp_t perfbench_stamp(void) {
    perfbench_stamp_t stamp;
    stamp.cycles = g_tsc_supported ? perfmon_read_tsc() : 0ULL;
    stamp.ticks = pit_get_ticks();
    return stamp;
}

static uint32_t perfbench_elapsed_us(perfbench_stamp_t started,
                                     uint32_t cycles_per_us,
                                     uint32_t hz) {
    uint32_t ticks = pit_get_ticks() - started.ticks;
    if (g_tsc_supported && cycles_per_us) {
        uint64_t cycles = perfmon_read_tsc() - started.cycles;
        uint32_t us = perfmon_cycles_to_us(cycles, cycles_per_us);
        if (us) return us;
    }
    if (!hz) hz = 1U;
    if (!ticks) return 1U;
    return perfmon_u64_to_u32(((uint64_t)ticks * 1000000ULL) / hz);
}

static uint32_t perfbench_calibrate_tsc(uint32_t hz) {
    uint32_t start_tick;
    uint32_t wait_ticks;
    uint32_t elapsed_ticks;
    uint64_t start_cycles;
    uint64_t elapsed_cycles;
    uint64_t elapsed_us;

    if (!g_tsc_supported || !hz) return 0U;
    start_tick = pit_get_ticks();
    while (pit_get_ticks() == start_tick) __asm__ volatile ("pause");
    start_tick = pit_get_ticks();
    wait_ticks = hz / 20U; /* aproximadamente 50 ms */
    if (wait_ticks < 3U) wait_ticks = 3U;
    start_cycles = perfmon_read_tsc();
    while (pit_get_ticks() - start_tick < wait_ticks)
        __asm__ volatile ("pause");
    elapsed_cycles = perfmon_read_tsc() - start_cycles;
    elapsed_ticks = pit_get_ticks() - start_tick;
    elapsed_us = ((uint64_t)elapsed_ticks * 1000000ULL) / hz;
    if (!elapsed_us) return 0U;
    return perfmon_u64_to_u32(elapsed_cycles / elapsed_us);
}

static uint32_t perfbench_rate(uint64_t units, uint32_t elapsed_us,
                               uint32_t scale) {
    if (!elapsed_us) return 0U;
    return perfmon_u64_to_u32((units * (uint64_t)scale) / elapsed_us);
}

static uint32_t perfbench_mib_per_second(uint64_t bytes,
                                         uint32_t elapsed_us) {
    if (!elapsed_us) return 0U;
    return perfmon_u64_to_u32(
        (bytes * 1000000ULL) / ((uint64_t)elapsed_us * 1024ULL * 1024ULL));
}

static uint32_t perfbench_score(uint32_t value, uint32_t reference) {
    uint64_t score;
    if (!reference) return 0U;
    score = ((uint64_t)value * 100ULL) / reference;
    return score > 100ULL ? 100U : (uint32_t)score;
}

static void perfbench_add_score(uint32_t score, uint32_t weight,
                                uint32_t *weighted, uint32_t *weights) {
    if (!weighted || !weights || !weight) return;
    *weighted += score * weight;
    *weights += weight;
}

static block_device_t *perfbench_disk(void) {
    block_device_t *fallback = NULL;
    uint32_t count = block_count();
    for (uint32_t i = 0; i < count; i++) {
        block_device_t *dev = block_at(i);
        if (!dev || !dev->read || !dev->sector_count || !dev->sector_size)
            continue;
        if (dev->type == BLOCK_DEVICE_ATA) return dev;
        if (!fallback && dev->type == BLOCK_DEVICE_USB) fallback = dev;
    }
    return fallback;
}

static const char *perfbench_grade(uint32_t score) {
    if (score >= 90U) return "excelente";
    if (score >= 75U) return "bueno";
    if (score >= 60U) return "aceptable";
    if (score >= 40U) return "lento";
    return "muy lento";
}

static int perfmon_run_benchmark_internal(
    perfmon_benchmark_result_t *result, bool print_output) {
    const uint32_t cpu_iterations = 4000000U;
    const uint32_t memory_size = 256U * 1024U;
    const uint32_t memory_passes = 64U;
    const uint32_t draw_width = 320U;
    const uint32_t draw_height = 200U;
    const uint32_t draw_passes = 100U;
    const uint32_t scheduler_yields = 1000U;
    uint32_t hz = pit_get_frequency_hz();
    uint32_t cycles_per_us;
    uint32_t weighted = 0U;
    uint32_t weights = 0U;
    uint32_t lowest_score = 101U;
    const char *lowest_name = "ninguno";
    bool previous_reporting;
    volatile uint32_t cpu_a = 0x12345678U;
    volatile uint32_t cpu_b = 0x9E3779B9U;
    volatile uint32_t cpu_c = 0xA5A5A5A5U;
    perfbench_stamp_t stamp;
    uint32_t elapsed_us;
    uint32_t cpu_kiter_s;
    uint32_t cpu_mhz;
    uint32_t cpu_score;
    uint8_t *memory_a = NULL;
    uint8_t *memory_b = NULL;
    uint32_t copy_mib_s = 0U;
    uint32_t fill_mib_s = 0U;
    uint32_t memory_score = 0U;
    uint32_t *draw_pixels = NULL;
    uint32_t draw_mpix_s = 0U;
    uint32_t draw_score = 0U;
    uint32_t present_mib_s = 0U;
    uint32_t present_score = 0U;
    bool present_available = false;
    uint32_t disk_mib_s = 0U;
    uint32_t disk_score = 0U;
    bool disk_available = false;
    uint32_t scheduler_per_s;
    uint32_t scheduler_score;
    perfmon_benchmark_result_t local_result;

    if (!result) result = &local_result;
    kmemset(result, 0, sizeof(*result));
    result->struct_size = sizeof(*result);
    if (!hz) hz = 1U;
    if (g_reporting) {
        if (print_output) kprintf("benchmark: el perfilador ya esta generando un reporte\n");
        return 1;
    }

    previous_reporting = g_reporting;
    g_reporting = true; /* silencia perfmon y excluye el propio benchmark */
    if (print_output) kprintf("[BENCH] BlesKernOS: CPU, RAM, dibujo, video, disco y scheduler\n");
    if (print_output) kprintf("[BENCH] La puntuacion compara esta misma version del sistema; "
            "no escribe en disco.\n");

    cycles_per_us = perfbench_calibrate_tsc(hz);
    cpu_mhz = cycles_per_us;

    stamp = perfbench_stamp();
    for (uint32_t i = 0; i < cpu_iterations; i++) {
        cpu_a = cpu_a * 1664525U + 1013904223U;
        cpu_b ^= (cpu_a >> 7) | (cpu_a << 25);
        cpu_c += (cpu_b ^ i) + (cpu_a >> 11);
        cpu_a ^= cpu_c + (cpu_b << 3);
    }
    elapsed_us = perfbench_elapsed_us(stamp, cycles_per_us, hz);
    cpu_kiter_s = perfbench_rate(cpu_iterations, elapsed_us, 1000U);
    cpu_score = (perfbench_score(cpu_mhz, 1000U) +
                 perfbench_score(cpu_kiter_s, 25000U)) / 2U;
    perfbench_add_score(cpu_score, 20U, &weighted, &weights);
    if (cpu_score < lowest_score) lowest_score = cpu_score, lowest_name = "CPU";
    if (print_output) kprintf("[BENCH:CPU] %u MHz mix=%u Kiter/s score=%u/100\n",
            cpu_mhz, cpu_kiter_s, cpu_score);
    result->available_mask |= PERFMON_BENCH_CPU;
    result->cpu_mhz = cpu_mhz;
    result->cpu_kiter_s = cpu_kiter_s;
    result->cpu_score = cpu_score;

    memory_a = (uint8_t *)kmalloc(memory_size);
    memory_b = (uint8_t *)kmalloc(memory_size);
    if (memory_a && memory_b) {
        for (uint32_t i = 0; i < memory_size; i++)
            memory_a[i] = (uint8_t)(i * 37U + 11U);
        stamp = perfbench_stamp();
        for (uint32_t pass = 0; pass < memory_passes; pass++) {
            memory_a[pass & (memory_size - 1U)] ^= (uint8_t)pass;
            kmemcpy(memory_b, memory_a, memory_size);
        }
        elapsed_us = perfbench_elapsed_us(stamp, cycles_per_us, hz);
        copy_mib_s = perfbench_mib_per_second(
            (uint64_t)memory_size * memory_passes, elapsed_us);

        stamp = perfbench_stamp();
        for (uint32_t pass = 0; pass < memory_passes; pass++)
            kmemset(memory_b, (int)pass, memory_size);
        elapsed_us = perfbench_elapsed_us(stamp, cycles_per_us, hz);
        fill_mib_s = perfbench_mib_per_second(
            (uint64_t)memory_size * memory_passes, elapsed_us);
        memory_score = (perfbench_score(copy_mib_s, 160U) +
                        perfbench_score(fill_mib_s, 200U)) / 2U;
        perfbench_add_score(memory_score, 15U, &weighted, &weights);
        if (memory_score < lowest_score)
            lowest_score = memory_score, lowest_name = "RAM/memoria";
        if (print_output) kprintf("[BENCH:RAM] copy=%u MiB/s fill=%u MiB/s score=%u/100\n",
                copy_mib_s, fill_mib_s, memory_score);
        result->available_mask |= PERFMON_BENCH_MEMORY;
        result->memory_copy_mib_s = copy_mib_s;
        result->memory_fill_mib_s = fill_mib_s;
        result->memory_score = memory_score;
    } else {
        if (print_output) kprintf("[BENCH:RAM] omitido: memoria insuficiente\n");
    }
    if (memory_a) kfree(memory_a);
    if (memory_b) kfree(memory_b);

    draw_pixels = (uint32_t *)kmalloc(
        draw_width * draw_height * sizeof(uint32_t));
    if (draw_pixels) {
        gui_surface_t surface = {
            .pixels = draw_pixels,
            .width = (uint16_t)draw_width,
            .height = (uint16_t)draw_height,
            .pitch = (uint16_t)draw_width,
            .clip = {0, 0, (int)draw_width, (int)draw_height}
        };
        stamp = perfbench_stamp();
        for (uint32_t pass = 0; pass < draw_passes; pass++) {
            uint32_t color = ((pass * 17U) << 16) |
                             ((pass * 29U) << 8) | (pass * 43U);
            gui_gfx_fill_rect(&surface,
                (gui_rect_t){0, 0, (int)draw_width, (int)draw_height}, color);
            gui_gfx_draw_line(&surface, 0, (int)(pass % draw_height),
                              (int)draw_width - 1,
                              (int)(draw_height - 1U - pass % draw_height),
                              color ^ 0x00FFFFFFU);
        }
        elapsed_us = perfbench_elapsed_us(stamp, cycles_per_us, hz);
        draw_mpix_s = perfbench_rate(
            (uint64_t)draw_width * draw_height * draw_passes,
            elapsed_us, 1U);
        draw_score = perfbench_score(draw_mpix_s, 60U);
        perfbench_add_score(draw_score, 10U, &weighted, &weights);
        if (draw_score < lowest_score)
            lowest_score = draw_score, lowest_name = "dibujo software";
        if (print_output) kprintf("[BENCH:DRAW] %u MPix/s score=%u/100\n",
                draw_mpix_s, draw_score);
        result->available_mask |= PERFMON_BENCH_DRAW;
        result->draw_mpix_s = draw_mpix_s;
        result->draw_score = draw_score;
        kfree(draw_pixels);
    } else {
        if (print_output) kprintf("[BENCH:DRAW] omitido: memoria insuficiente\n");
    }

    {
        gui_desktop_t *desktop = gui_get_desktop();
        if (desktop && desktop->surface.pixels && desktop->surface.width &&
            desktop->surface.height) {
            uint32_t width = desktop->surface.width < 160U
                ? desktop->surface.width : 160U;
            uint32_t height = desktop->surface.height < 120U
                ? desktop->surface.height : 120U;
            uint32_t x = (desktop->surface.width - width) / 2U;
            uint32_t y = (desktop->surface.height - height) / 2U;
            uint32_t *backup = (uint32_t *)kmalloc(
                width * height * sizeof(uint32_t));
            const uint32_t passes = 12U;
            if (backup && width && height) {
                gui_rect_t rect = {(int)x, (int)y, (int)width, (int)height};
                gui_desktop_paint_lock();
                for (uint32_t row = 0; row < height; row++)
                    kmemcpy(&backup[row * width],
                            &desktop->surface.pixels[(y + row) *
                                desktop->surface.pitch + x],
                            width * sizeof(uint32_t));
                stamp = perfbench_stamp();
                for (uint32_t pass = 0; pass < passes; pass++) {
                    uint32_t color = ((pass * 41U) << 16) |
                                     ((pass * 67U) << 8) | (pass * 97U);
                    gui_gfx_fill_rect(&desktop->surface, rect, color);
                    gui_gfx_present_rect(&desktop->surface, rect);
                    gfx_flush();
                }
                elapsed_us = perfbench_elapsed_us(stamp, cycles_per_us, hz);
                for (uint32_t row = 0; row < height; row++)
                    kmemcpy(&desktop->surface.pixels[(y + row) *
                                desktop->surface.pitch + x],
                            &backup[row * width], width * sizeof(uint32_t));
                gui_gfx_present_rect(&desktop->surface, rect);
                gfx_flush();
                gui_desktop_paint_unlock();
                kfree(backup);
                present_mib_s = perfbench_mib_per_second(
                    (uint64_t)width * height * sizeof(uint32_t) * passes,
                    elapsed_us);
                present_score = perfbench_score(present_mib_s, 35U);
                present_available = true;
                perfbench_add_score(present_score, 20U, &weighted, &weights);
                if (present_score < lowest_score)
                    lowest_score = present_score,
                    lowest_name = "video/framebuffer";
                if (print_output) kprintf("[BENCH:VIDEO] driver=%s present=%u MiB/s "
                        "score=%u/100\n", gfx_driver_name(), present_mib_s,
                        present_score);
                result->available_mask |= PERFMON_BENCH_VIDEO;
                result->video_present_mib_s = present_mib_s;
                result->video_score = present_score;
                perfmon_copy_text(result->gfx_driver,
                                  sizeof(result->gfx_driver),
                                  gfx_driver_name());
            } else {
                if (backup) kfree(backup);
                if (print_output) kprintf("[BENCH:VIDEO] omitido: sin buffer temporal\n");
            }
        } else {
            if (print_output) kprintf("[BENCH:VIDEO] omitido: GUI no disponible\n");
        }
    }

    {
        block_device_t *disk = perfbench_disk();
        if (disk && disk->sector_size && disk->sector_count >= 16U) {
            uint32_t sectors = 16U;
            uint32_t bytes_per_read;
            uint32_t passes;
            uint32_t base_lba;
            uint8_t *buffer;
            bool ok = true;
            if (sectors > disk->sector_count) sectors = disk->sector_count;
            bytes_per_read = sectors * disk->sector_size;
            buffer = (uint8_t *)kmalloc(bytes_per_read);
            passes = bytes_per_read ? (1024U * 1024U) / bytes_per_read : 0U;
            if (passes < 8U) passes = 8U;
            if (passes > 128U) passes = 128U;
            base_lba = disk->sector_count > 4096U ? 2048U : 0U;
            if (buffer) {
                stamp = perfbench_stamp();
                for (uint32_t pass = 0; pass < passes; pass++) {
                    uint32_t span = disk->sector_count - sectors;
                    uint32_t lba = span
                        ? (base_lba + pass * sectors) % span : 0U;
                    if (!block_read(disk, lba, (uint8_t)sectors, buffer)) {
                        ok = false;
                        break;
                    }
                }
                elapsed_us = perfbench_elapsed_us(stamp, cycles_per_us, hz);
                if (ok) {
                    disk_mib_s = perfbench_mib_per_second(
                        (uint64_t)bytes_per_read * passes, elapsed_us);
                    disk_score = perfbench_score(disk_mib_s, 5U);
                    disk_available = true;
                    perfbench_add_score(disk_score, 15U,
                                        &weighted, &weights);
                    if (disk_score < lowest_score)
                        lowest_score = disk_score, lowest_name = "disco/E-S";
                    if (print_output) kprintf("[BENCH:DISK] %s read=%u MiB/s score=%u/100\n",
                            disk->name, disk_mib_s, disk_score);
                    result->available_mask |= PERFMON_BENCH_DISK;
                    result->disk_read_mib_s = disk_mib_s;
                    result->disk_score = disk_score;
                    perfmon_copy_text(result->disk_name,
                                      sizeof(result->disk_name), disk->name);
                } else {
                    if (print_output) kprintf("[BENCH:DISK] %s fallo de lectura; score=0/100\n",
                            disk->name);
                    disk_available = true;
                    disk_score = 0U;
                    result->available_mask |= PERFMON_BENCH_DISK;
                    result->disk_score = 0U;
                    perfmon_copy_text(result->disk_name,
                                      sizeof(result->disk_name), disk->name);
                    perfbench_add_score(0U, 15U, &weighted, &weights);
                    lowest_score = 0U;
                    lowest_name = "disco/E-S";
                }
                kfree(buffer);
            } else {
                if (print_output) kprintf("[BENCH:DISK] omitido: memoria insuficiente\n");
            }
        } else {
            if (print_output) kprintf("[BENCH:DISK] omitido: no hay ATA/USB legible\n");
        }
    }

    stamp = perfbench_stamp();
    for (uint32_t i = 0; i < scheduler_yields; i++) task_yield();
    elapsed_us = perfbench_elapsed_us(stamp, cycles_per_us, hz);
    scheduler_per_s = perfbench_rate(scheduler_yields, elapsed_us, 1000000U);
    scheduler_score = perfbench_score(scheduler_per_s, 10000U);
    perfbench_add_score(scheduler_score, 5U, &weighted, &weights);
    if (scheduler_score < lowest_score)
        lowest_score = scheduler_score, lowest_name = "scheduler";
    if (print_output) kprintf("[BENCH:SCHED] %u yields/s score=%u/100\n",
            scheduler_per_s, scheduler_score);
    result->available_mask |= PERFMON_BENCH_SCHEDULER;
    result->scheduler_yields_s = scheduler_per_s;
    result->scheduler_score = scheduler_score;

    {
        const uint32_t passes = 16U;
        vfs_dir_entry_t *entries = (vfs_dir_entry_t *)kmalloc(
            64U * sizeof(vfs_dir_entry_t));
        uint32_t last_count = 0U;
        bool ok = entries != NULL;
        uint32_t lists_s = 0U;
        uint32_t score = 0U;
        if (ok) {
            stamp = perfbench_stamp();
            for (uint32_t pass = 0; pass < passes; pass++) {
                if (!vfs_listdir("/SYSTEM/PROGRAMS", entries, 64U,
                                 &last_count)) {
                    ok = false;
                    break;
                }
            }
            elapsed_us = perfbench_elapsed_us(stamp, cycles_per_us, hz);
            if (ok) {
                lists_s = perfbench_rate(passes, elapsed_us, 1000000U);
                score = perfbench_score(lists_s, 80U);
                perfbench_add_score(score, 10U, &weighted, &weights);
                if (score < lowest_score)
                    lowest_score = score, lowest_name = "sistema de archivos";
                result->available_mask |= PERFMON_BENCH_FILESYSTEM;
                result->filesystem_lists_s = lists_s;
                result->filesystem_entries = last_count;
                result->filesystem_score = score;
                if (print_output) kprintf(
                    "[BENCH:FS] listdir=%u/s entries=%u score=%u/100\n",
                    lists_s, last_count, score);
            } else if (print_output) {
                kprintf("[BENCH:FS] fallo al listar /SYSTEM/PROGRAMS\n");
            }
            kfree(entries);
        } else if (print_output) {
            kprintf("[BENCH:FS] omitido: memoria insuficiente\n");
        }
    }

    {
        const uint32_t samples = 20U;
        uint32_t sleep_ticks = hz / 100U;
        uint32_t target_us;
        uint64_t total_late = 0ULL;
        uint32_t max_late = 0U;
        uint32_t score;
        if (!sleep_ticks) sleep_ticks = 1U;
        target_us = (uint32_t)(((uint64_t)sleep_ticks * 1000000ULL) / hz);
        for (uint32_t sample = 0; sample < samples; sample++) {
            stamp = perfbench_stamp();
            task_sleep(sleep_ticks);
            elapsed_us = perfbench_elapsed_us(stamp, cycles_per_us, hz);
            if (elapsed_us > target_us) {
                uint32_t late = elapsed_us - target_us;
                total_late += late;
                if (late > max_late) max_late = late;
            }
        }
        result->timer_average_late_us = (uint32_t)(total_late / samples);
        result->timer_max_late_us = max_late;
        {
            uint32_t penalty = result->timer_average_late_us / 200U +
                               result->timer_max_late_us / 500U;
            score = penalty >= 100U ? 0U : 100U - penalty;
        }
        result->available_mask |= PERFMON_BENCH_TIMER;
        result->timer_score = score;
        perfbench_add_score(score, 5U, &weighted, &weights);
        if (score < lowest_score)
            lowest_score = score, lowest_name = "temporizador/latencia";
        if (print_output) kprintf(
            "[BENCH:TIMER] late avg=%uus max=%uus score=%u/100\n",
            result->timer_average_late_us, result->timer_max_late_us, score);
    }

    {
        uint32_t total = weights ? weighted / weights : 0U;
        result->total_score = total;
        perfmon_copy_text(result->grade, sizeof(result->grade),
                          perfbench_grade(total));
        perfmon_copy_text(result->bottleneck, sizeof(result->bottleneck),
                          lowest_name);
        if (print_output) kprintf("[BENCH:TOTAL] %u/100 (%s) cuello=%s\n",
                total, perfbench_grade(total), lowest_name);
        if (present_available && present_score + 15U < cpu_score) {
            if (print_output) kprintf("[BENCH:HINT] CPU razonable pero video lento: revise "
                    "driver, bpp, dirty rects y copias al framebuffer.\n");
        } else if (disk_available && disk_score + 15U < cpu_score) {
            if (print_output) kprintf("[BENCH:HINT] E/S muy por debajo de CPU: revise ATA PIO, "
                    "timeouts, FAT y tormentas de IRQ.\n");
        } else if (memory_score && memory_score + 15U < cpu_score) {
            if (print_output) kprintf("[BENCH:HINT] RAM/copia lenta: revise cache CPU, REP MOVS "
                    "y alineacion.\n");
        } else {
            if (print_output) kprintf("[BENCH:HINT] Use 'perfmon now' durante la lentitud para "
                    "confirmar el cuello en uso real.\n");
        }
    }

    /* Evitar que el benchmark contamine la proxima captura manual. */
    g_reporting = previous_reporting;
    perfmon_reset();
    (void)cpu_a;
    (void)cpu_b;
    (void)cpu_c;
    return 0;
}

int perfmon_run_benchmark_capture(perfmon_benchmark_result_t *result) {
    if (!result) return 1;
    return perfmon_run_benchmark_internal(result, false);
}

int perfmon_run_benchmark(void) {
    return perfmon_run_benchmark_internal(NULL, true);
}

