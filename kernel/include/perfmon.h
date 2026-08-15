#ifndef PERFMON_H
#define PERFMON_H

#include "types.h"

/* Perfilador de bajo costo para diagnostico en hardware real. Los tiempos se
 * miden con TSC cuando la CPU lo anuncia; los contadores siguen funcionando
 * en CPUs sin TSC. Los reportes periodicos salen por el flujo normal de COM1. */

#define PERFMON_IO_TYPE_COUNT 5U
#define PERFMON_IRQ_COUNT 16U

#define PERFMON_BENCH_CPU       0x00000001U
#define PERFMON_BENCH_MEMORY    0x00000002U
#define PERFMON_BENCH_DRAW      0x00000004U
#define PERFMON_BENCH_VIDEO     0x00000008U
#define PERFMON_BENCH_DISK      0x00000010U
#define PERFMON_BENCH_SCHEDULER 0x00000020U
#define PERFMON_BENCH_FILESYSTEM 0x00000040U
#define PERFMON_BENCH_TIMER     0x00000080U

typedef struct {
    uint32_t read_calls;
    uint32_t read_sectors;
    uint32_t read_failures;
    uint32_t read_average_us;
    uint32_t read_max_us;
    uint32_t write_calls;
    uint32_t write_sectors;
    uint32_t write_failures;
    uint32_t write_average_us;
    uint32_t write_max_us;
} perfmon_io_snapshot_t;

typedef struct {
    uint32_t struct_size;
    uint32_t enabled;
    uint32_t tsc_supported;
    uint32_t elapsed_ms;
    uint32_t cpu_percent;
    uint32_t tsc_mhz;

    uint32_t memory_total_bytes;
    uint32_t memory_used_bytes;
    uint32_t memory_free_bytes;
    uint32_t heap_total_blocks;
    uint32_t heap_used_blocks;
    uint32_t heap_free_blocks;

    uint32_t gfx_width;
    uint32_t gfx_height;
    uint32_t gfx_bpp;
    uint32_t gfx_capabilities;
    char gfx_driver[24];

    uint32_t gui_loops;
    uint32_t gui_events;
    uint32_t gui_mouse_moves;
    uint32_t gui_frames;
    uint32_t gui_fps;
    uint32_t gui_content_frames;
    uint32_t gui_full_frames;
    uint32_t gui_cursor_only_frames;
    uint32_t gui_hw_cursor_frames;
    uint32_t gui_gpu_frames;
    uint32_t gui_dirty_percent;

    uint32_t frame_average_us;
    uint32_t frame_max_us;
    uint32_t frame_share_percent;
    uint32_t compose_average_us;
    uint32_t compose_max_us;
    uint32_t compose_share_percent;
    uint32_t present_average_us;
    uint32_t present_max_us;
    uint32_t present_share_percent;
    uint32_t gpu_average_us;
    uint32_t gpu_max_us;
    uint32_t gpu_share_percent;
    uint32_t fpu_average_us;
    uint32_t fpu_max_us;
    uint32_t fpu_share_percent;

    uint32_t scheduler_ticks;
    uint32_t scheduler_busy_ticks;
    uint32_t scheduler_switches;
    uint32_t scheduler_fast_quantum;
    uint32_t scheduler_preempt_blocked;

    uint32_t irq_total;
    uint32_t irq[PERFMON_IRQ_COUNT];
    perfmon_io_snapshot_t io[PERFMON_IO_TYPE_COUNT];
} perfmon_snapshot_t;

typedef struct {
    uint32_t struct_size;
    uint32_t available_mask;
    uint32_t total_score;
    uint32_t cpu_score;
    uint32_t memory_score;
    uint32_t draw_score;
    uint32_t video_score;
    uint32_t disk_score;
    uint32_t scheduler_score;
    uint32_t filesystem_score;
    uint32_t timer_score;

    uint32_t cpu_mhz;
    uint32_t cpu_kiter_s;
    uint32_t memory_copy_mib_s;
    uint32_t memory_fill_mib_s;
    uint32_t draw_mpix_s;
    uint32_t video_present_mib_s;
    uint32_t disk_read_mib_s;
    uint32_t scheduler_yields_s;
    uint32_t filesystem_lists_s;
    uint32_t filesystem_entries;
    uint32_t timer_average_late_us;
    uint32_t timer_max_late_us;

    char grade[16];
    char bottleneck[32];
    char gfx_driver[24];
    char disk_name[16];
} perfmon_benchmark_result_t;

typedef enum {
    PERF_SCOPE_GUI_EVENTS = 0,
    PERF_SCOPE_GUI_FRAME,
    PERF_SCOPE_GUI_COMPOSE,
    PERF_SCOPE_GFX_PRESENT,
    PERF_SCOPE_GPU_PRESENT,
    PERF_SCOPE_BLOCK_READ,
    PERF_SCOPE_BLOCK_WRITE,
    PERF_SCOPE_SCHEDULER,
    PERF_SCOPE_FPU_SWITCH,
    PERF_SCOPE_COUNT
} perf_scope_id_t;

void perfmon_init(void);
bool perfmon_enabled(void);
void perfmon_set_enabled(bool enabled);
void perfmon_reset(void);
void perfmon_force_report(void);
void perfmon_set_interval_seconds(uint32_t seconds);
uint32_t perfmon_interval_seconds(void);

uint64_t perfmon_scope_begin(void);
void perfmon_scope_end(perf_scope_id_t scope, uint64_t started);

void perfmon_irq(uint8_t irq);
void perfmon_scheduler_tick(bool busy);
void perfmon_scheduler_switch(void);
void perfmon_scheduler_fast_quantum(void);
void perfmon_scheduler_preempt_blocked(void);

void perfmon_gui_loop(void);
void perfmon_gui_event(bool mouse_move);
void perfmon_gui_frame(uint32_t dirty_pixels, uint32_t screen_pixels,
                       bool content_dirty, bool hardware_cursor,
                       bool gpu_presented);

void perfmon_block_complete(uint32_t device_type, bool write,
                            uint8_t sectors, bool success,
                            uint64_t started);

/* Llamar desde el loop principal, no desde una IRQ: decide si corresponde
 * emitir un snapshot compacto y reinicia los contadores del intervalo. */
void perfmon_poll(void);

/* Ejecuta una prueba sintetica y segura desde la terminal. */
bool perfmon_capture_snapshot(perfmon_snapshot_t *snapshot,
                              bool reset_interval);
int perfmon_run_benchmark_capture(perfmon_benchmark_result_t *result);
int perfmon_run_benchmark(void);

#endif
