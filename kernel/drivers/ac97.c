#include "../include/driver.h"
#include "../include/memory.h"
#include "../include/pci.h"
#include "../include/pic.h"
#include "../include/pit.h"
#include "../include/sound.h"
#include "../include/task.h"
#include "../include/vga.h"

#define AC97_NAM_RESET       0x00U
#define AC97_NAM_MASTER_VOL  0x02U
#define AC97_NAM_PCM_VOL     0x18U
#define AC97_NAM_VENDOR1     0x7CU
#define AC97_NAM_VENDOR2     0x7EU

#define AC97_PO_BDBAR        0x10U
#define AC97_PO_CIV          0x14U
#define AC97_PO_LVI          0x15U
#define AC97_PO_SR           0x16U
#define AC97_PO_SIS_SR       0x18U
#define AC97_PO_CR           0x1BU
#define AC97_GLOB_CNT        0x2CU
#define AC97_GLOB_STA        0x30U

#define AC97_SR_DCH          0x0001U
#define AC97_SR_FIFOE        0x0010U
#define AC97_SR_CLEAR        0x001CU
#define AC97_CR_RUN          0x01U
#define AC97_CR_RESET        0x02U
#define AC97_GLOB_COLD       0x00000002U
#define AC97_GLOB_PCR        0x00000100U

#define AC97_BDL_COUNT       32U
#define AC97_BUFFER_FRAMES   2048U
#define AC97_PRELOAD_SHORT   4U
#define AC97_PRELOAD_LONG    12U
#define AC97_OUTPUT_HZ       48000U
#define AC97_MIX_VOICES      8U
#define AC97_BDL_BUP         0x4000U
#define AC97_TONE_MAX_MS     1000U
#define AC97_DIRECT_MIN_FRAMES (AC97_OUTPUT_HZ * 2U)

typedef struct PACKED {
    uint32_t address;
    uint16_t samples;
    uint16_t control;
} ac97_bdl_entry_t;

typedef struct {
    const uint8_t *samples;
    uint32_t length;
    uint64_t position_fp;
    uint32_t step_fp;
    uint8_t volume;
    volatile bool active;
} ac97_voice_t;

typedef enum {
    AC97_CONTROLLER_ICH = 0,
    AC97_CONTROLLER_NFORCE,
    AC97_CONTROLLER_SIS7012
} ac97_controller_type_t;

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    ac97_controller_type_t type;
    const char *name;
} ac97_controller_id_t;

/* Motores PCM bus-master derivados de Intel ICH. Tener un codec AC'97 no
 * basta: ALi, VIA y ESS usan registros/DSP diferentes. */
static const ac97_controller_id_t g_ac97_controller_ids[] = {
    {0x8086, 0x2415, AC97_CONTROLLER_ICH, "Intel ICH"},
    {0x8086, 0x2425, AC97_CONTROLLER_ICH, "Intel ICH0"},
    {0x8086, 0x2445, AC97_CONTROLLER_ICH, "Intel ICH2"},
    {0x8086, 0x2485, AC97_CONTROLLER_ICH, "Intel ICH3"},
    {0x8086, 0x24C5, AC97_CONTROLLER_ICH, "Intel ICH4"},
    {0x8086, 0x24D5, AC97_CONTROLLER_ICH, "Intel ICH5"},
    {0x8086, 0x25A6, AC97_CONTROLLER_ICH, "Intel 6300ESB"},
    {0x8086, 0x266E, AC97_CONTROLLER_ICH, "Intel ICH6"},
    {0x8086, 0x27DE, AC97_CONTROLLER_ICH, "Intel ICH7"},
    {0x8086, 0x2698, AC97_CONTROLLER_ICH, "Intel ESB2"},
    {0x8086, 0x7195, AC97_CONTROLLER_ICH, "Intel 440MX"},
    {0x1022, 0x7445, AC97_CONTROLLER_ICH, "AMD-768"},
    {0x1022, 0x746D, AC97_CONTROLLER_ICH, "AMD-8111"},
    {0x10DE, 0x01B1, AC97_CONTROLLER_NFORCE, "NVIDIA nForce"},
    {0x10DE, 0x003A, AC97_CONTROLLER_NFORCE, "NVIDIA MCP04"},
    {0x10DE, 0x006A, AC97_CONTROLLER_NFORCE, "NVIDIA nForce2"},
    {0x10DE, 0x0059, AC97_CONTROLLER_NFORCE, "NVIDIA CK804"},
    {0x10DE, 0x008A, AC97_CONTROLLER_NFORCE, "NVIDIA CK8"},
    {0x10DE, 0x00DA, AC97_CONTROLLER_NFORCE, "NVIDIA nForce3"},
    {0x10DE, 0x00EA, AC97_CONTROLLER_NFORCE, "NVIDIA CK8S"},
    {0x10DE, 0x026B, AC97_CONTROLLER_NFORCE, "NVIDIA MCP51"},
    {0x1039, 0x7012, AC97_CONTROLLER_SIS7012, "SiS 7012"}
};

static const pci_device_t *g_ac97_pci;
static const ac97_controller_id_t *g_ac97_controller;
static uint16_t g_ac97_nam;
static uint16_t g_ac97_nabm;
static uint16_t g_ac97_po_sr = AC97_PO_SR;
static ac97_bdl_entry_t g_ac97_bdl[AC97_BDL_COUNT]
    __attribute__((aligned(128)));
static int16_t g_ac97_buffers[AC97_BDL_COUNT][AC97_BUFFER_FRAMES * 2U]
    __attribute__((aligned(4096)));
static ac97_voice_t g_ac97_voices[AC97_MIX_VOICES];
static uint8_t g_ac97_tone[AC97_OUTPUT_HZ]
    __attribute__((aligned(16)));
static volatile bool g_ac97_present;
static volatile bool g_ac97_streaming;
static bool g_ac97_observed_running;
static uint32_t g_ac97_start_tick;
static uint32_t g_ac97_producer_seq;
static uint32_t g_ac97_current_seq;
static uint8_t g_ac97_last_civ;
static uint32_t g_ac97_fifo_errors;
static uint32_t g_ac97_dch_restarts;
static int16_t *g_ac97_direct_buffer;
static volatile bool g_ac97_direct_active;

static void ac97_channel_reset(void);
static uint16_t ac97_descriptor_length(uint32_t frames);
static uint32_t ac97_max_descriptor_frames(void);

static inline uint16_t ac97_inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void ac97_outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void ac97_io_wait(void) {
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}

static inline void ac97_dma_publish(void) {
    /* Publicar contenido/BDL antes de que el dispositivo vea un LVI nuevo. */
    __asm__ volatile ("" : : : "memory");
}

static void ac97_prepare_ring_bdl(void) {
    for (uint32_t i = 0; i < AC97_BDL_COUNT; i++) {
        g_ac97_bdl[i].address =
            (uint32_t)(uintptr_t)g_ac97_buffers[i];
        g_ac97_bdl[i].samples = ac97_descriptor_length(AC97_BUFFER_FRAMES);
        g_ac97_bdl[i].control = AC97_BDL_BUP;
    }
    ac97_dma_publish();
}

static const ac97_controller_id_t *ac97_match_controller(
        const pci_device_t *device) {
    if (!device) return NULL;
    for (uint32_t i = 0; i < sizeof(g_ac97_controller_ids) /
                                  sizeof(g_ac97_controller_ids[0]); i++) {
        const ac97_controller_id_t *id = &g_ac97_controller_ids[i];
        if (device->vendor_id == id->vendor_id &&
            device->device_id == id->device_id) return id;
    }
    return NULL;
}

static const pci_device_t *ac97_find_device(void) {
    uint32_t count = pci_device_count();
    for (uint32_t i = 0; i < count; i++) {
        const pci_device_t *dev = pci_device_at(i);
        if (!dev || dev->class_code != 0x04U || dev->subclass != 0x01U)
            continue;
        g_ac97_controller = ac97_match_controller(dev);
        if (g_ac97_controller) return dev;
        if (dev->vendor_id == 0x125DU &&
            (dev->device_id == 0x1988U || dev->device_id == 0x1998U ||
             dev->device_id == 0x199AU)) {
            kprintf("[AC97] ESS %x:%x requiere Maestro3 + firmware DSP; no es ICH\n",
                    dev->vendor_id, dev->device_id);
        } else {
            kprintf("[AC97] audio PCI %x:%x usa otro motor DMA AC'97\n",
                    dev->vendor_id, dev->device_id);
        }
    }
    return NULL;
}

static uint16_t ac97_descriptor_length(uint32_t frames) {
    uint32_t length = frames * 2U; /* muestras stereo para ICH/nForce */
    if (g_ac97_controller &&
        g_ac97_controller->type == AC97_CONTROLLER_SIS7012)
        length *= sizeof(int16_t); /* SiS cuenta bytes */
    return (uint16_t)length;
}

static uint32_t ac97_max_descriptor_frames(void) {
    return g_ac97_controller &&
           g_ac97_controller->type == AC97_CONTROLLER_SIS7012
         ? 16383U : 32767U;
}

static bool ac97_has_active_voice(void) {
    for (uint32_t i = 0; i < AC97_MIX_VOICES; i++)
        if (g_ac97_voices[i].active) return true;
    return false;
}

static uint32_t ac97_preload_target(void) {
    uint64_t short_limit =
        (uint64_t)AC97_BUFFER_FRAMES * 8U;
    for (uint32_t i = 0; i < AC97_MIX_VOICES; i++) {
        const ac97_voice_t *voice = &g_ac97_voices[i];
        uint64_t remaining_fp;
        uint64_t short_source_fp;
        if (!voice->active || !voice->step_fp) continue;
        remaining_fp = ((uint64_t)voice->length << 16) -
                       voice->position_fp;
        short_source_fp = short_limit * voice->step_fp;
        if (remaining_fp > short_source_fp) return AC97_PRELOAD_LONG;
    }
    return AC97_PRELOAD_SHORT;
}

static void ac97_clear_voices(void) {
    for (uint32_t i = 0; i < AC97_MIX_VOICES; i++) {
        g_ac97_voices[i].active = false;
        g_ac97_voices[i].samples = NULL;
        g_ac97_voices[i].length = 0;
        g_ac97_voices[i].position_fp = 0;
        g_ac97_voices[i].step_fp = 0;
        g_ac97_voices[i].volume = 0;
    }
}

static bool ac97_add_voice(const uint8_t *samples, uint32_t length,
                           uint16_t rate, uint8_t volume) {
    uint32_t slot = AC97_MIX_VOICES;
    uint32_t furthest = 0;

    if (!samples || !length || rate < 4000U || rate > 48000U) return false;
    task_preempt_disable();
    for (uint32_t i = 0; i < AC97_MIX_VOICES; i++) {
        uint32_t position;
        if (!g_ac97_voices[i].active) {
            slot = i;
            break;
        }
        position = (uint32_t)(g_ac97_voices[i].position_fp >> 16);
        if (position >= furthest) {
            furthest = position;
            slot = i;
        }
    }
    if (slot < AC97_MIX_VOICES) {
        ac97_voice_t *voice = &g_ac97_voices[slot];
        voice->samples = samples;
        voice->length = length;
        voice->position_fp = 0;
        voice->step_fp = ((uint32_t)rate << 16) / AC97_OUTPUT_HZ;
        if (!voice->step_fp) voice->step_fp = 1;
        voice->volume = volume;
        voice->active = true;
    }
    task_preempt_enable();
    return slot < AC97_MIX_VOICES;
}

static int ac97_clamp_mix(int value) {
    if (value < -128) return -128;
    if (value > 127) return 127;
    return value;
}

static uint32_t ac97_render(int16_t *output, uint32_t max_frames) {
    uint32_t produced = 0;
    if (!output) return 0;

    for (uint32_t frame = 0; frame < max_frames; frame++) {
        int mixed = 0;
        bool any = false;
        for (uint32_t v = 0; v < AC97_MIX_VOICES; v++) {
            ac97_voice_t *voice = &g_ac97_voices[v];
            uint32_t position;
            uint32_t fraction;
            uint32_t next_position;
            int first;
            int second;
            int sample;
            if (!voice->active) continue;
            position = (uint32_t)(voice->position_fp >> 16);
            if (position >= voice->length) {
                voice->active = false;
                continue;
            }
            fraction = (uint32_t)(voice->position_fp & 0xFFFFU);
            next_position = position + 1U;
            if (next_position >= voice->length) next_position = position;
            first = (int)voice->samples[position] - 128;
            second = (int)voice->samples[next_position] - 128;
            sample = first + (((second - first) * (int)fraction) / 65536);
            mixed += (sample * (int)voice->volume) / 255;
            voice->position_fp += voice->step_fp;
            if ((voice->position_fp >> 16) >= voice->length)
                voice->active = false;
            any = true;
        }
        if (!any) break;
        mixed = ac97_clamp_mix(mixed) * 256;
        output[frame * 2U] = (int16_t)mixed;
        output[frame * 2U + 1U] = (int16_t)mixed;
        produced++;
    }
    return produced;
}

static uint32_t ac97_output_frames(uint32_t source_samples, uint16_t rate) {
    uint32_t seconds;
    uint32_t remainder;
    uint32_t frames;
    if (!rate) return 0;
    seconds = source_samples / rate;
    remainder = source_samples % rate;
    if (seconds > (AC97_BDL_COUNT * ac97_max_descriptor_frames()) /
                  AC97_OUTPUT_HZ) return 0;
    frames = seconds * AC97_OUTPUT_HZ;
    frames += (remainder * AC97_OUTPUT_HZ + rate - 1U) / rate;
    return frames;
}

static bool ac97_start_direct(const uint8_t *samples, uint32_t length,
                              uint16_t rate, uint8_t volume) {
    uint32_t frames;
    uint32_t step_fp;
    uint64_t position_fp = 0;
    uint32_t descriptor_count;
    uint32_t frame_offset = 0;
    uint32_t max_descriptor_frames;
    int16_t *output;

    if (!samples || !length || rate < 4000U || rate > 48000U)
        return false;
    max_descriptor_frames = ac97_max_descriptor_frames();
    frames = ac97_output_frames(length, rate);
    if (frames < AC97_DIRECT_MIN_FRAMES ||
        frames > AC97_BDL_COUNT * max_descriptor_frames || g_ac97_streaming ||
        ac97_has_active_voice()) return false;
    output = (int16_t *)kmalloc(frames * 2U * sizeof(int16_t));
    if (!output) return false;
    step_fp = ((uint32_t)rate << 16) / AC97_OUTPUT_HZ;
    if (!step_fp) step_fp = 1;

    for (uint32_t frame = 0; frame < frames; frame++) {
        uint32_t position = (uint32_t)(position_fp >> 16);
        uint32_t next_position;
        uint32_t fraction;
        int first;
        int second;
        int value;
        if (position >= length) position = length - 1U;
        next_position = position + 1U;
        if (next_position >= length) next_position = position;
        fraction = (uint32_t)(position_fp & 0xFFFFU);
        first = (int)samples[position] - 128;
        second = (int)samples[next_position] - 128;
        value = first + (((second - first) * (int)fraction) / 65536);
        value = (value * (int)volume) / 255;
        value = ac97_clamp_mix(value) * 256;
        output[frame * 2U] = (int16_t)value;
        output[frame * 2U + 1U] = (int16_t)value;
        position_fp += step_fp;
    }

    descriptor_count = (frames + max_descriptor_frames - 1U) /
                       max_descriptor_frames;
    kmemset(g_ac97_bdl, 0, sizeof(g_ac97_bdl));
    for (uint32_t i = 0; i < descriptor_count; i++) {
        uint32_t chunk_frames = frames - frame_offset;
        if (chunk_frames > max_descriptor_frames)
            chunk_frames = max_descriptor_frames;
        g_ac97_bdl[i].address = (uint32_t)(uintptr_t)
            &output[frame_offset * 2U];
        g_ac97_bdl[i].samples = ac97_descriptor_length(chunk_frames);
        g_ac97_bdl[i].control = AC97_BDL_BUP;
        frame_offset += chunk_frames;
    }

    ac97_channel_reset();
    g_ac97_direct_buffer = output;
    g_ac97_direct_active = true;
    g_ac97_producer_seq = descriptor_count;
    g_ac97_current_seq = 0;
    g_ac97_last_civ = 0;
    ac97_dma_publish();
    outl((uint16_t)(g_ac97_nabm + AC97_PO_BDBAR),
         (uint32_t)(uintptr_t)g_ac97_bdl);
    outb((uint16_t)(g_ac97_nabm + AC97_PO_LVI),
         (uint8_t)(descriptor_count - 1U));
    outb((uint16_t)(g_ac97_nabm + AC97_PO_CR), AC97_CR_RUN);
    g_ac97_observed_running = false;
    g_ac97_start_tick = pit_get_ticks();
    g_ac97_streaming = true;
    kprintf("[AC97] DMA directo: %u frames, %u descriptores\n",
            frames, descriptor_count);
    return true;
}

static bool ac97_fill_descriptor(uint32_t sequence) {
    uint32_t index = sequence & (AC97_BDL_COUNT - 1U);
    uint32_t frames = ac97_render(g_ac97_buffers[index],
                                  AC97_BUFFER_FRAMES);
    if (!frames) return false;
    if (frames < AC97_BUFFER_FRAMES) {
        kmemset(&g_ac97_buffers[index][frames * 2U], 0,
                (AC97_BUFFER_FRAMES - frames) * 2U * sizeof(int16_t));
    }
    return true;
}

static void ac97_channel_reset(void) {
    outb((uint16_t)(g_ac97_nabm + AC97_PO_CR), 0);
    outb((uint16_t)(g_ac97_nabm + AC97_PO_CR), AC97_CR_RESET);
    for (uint32_t i = 0; i < 100000U; i++) {
        if (!(inb((uint16_t)(g_ac97_nabm + AC97_PO_CR)) & AC97_CR_RESET))
            break;
        ac97_io_wait();
    }
    ac97_outw((uint16_t)(g_ac97_nabm + g_ac97_po_sr), AC97_SR_CLEAR);
}

static bool ac97_start_stream(void) {
    uint32_t filled = 0;
    uint32_t target;
    ac97_channel_reset();
    g_ac97_producer_seq = 0;
    g_ac97_current_seq = 0;
    g_ac97_last_civ = 0;

    target = ac97_preload_target();
    while (filled < target && ac97_fill_descriptor(filled)) filled++;
    if (!filled) return false;
    g_ac97_producer_seq = filled;
    ac97_dma_publish();
    outl((uint16_t)(g_ac97_nabm + AC97_PO_BDBAR),
         (uint32_t)(uintptr_t)g_ac97_bdl);
    outb((uint16_t)(g_ac97_nabm + AC97_PO_LVI),
         (uint8_t)((filled - 1U) & (AC97_BDL_COUNT - 1U)));
    outb((uint16_t)(g_ac97_nabm + AC97_PO_CR), AC97_CR_RUN);
    g_ac97_observed_running = false;
    g_ac97_start_tick = pit_get_ticks();
    g_ac97_streaming = true;
    return true;
}

static void ac97_fill_ahead(void) {
    uint32_t target = ac97_preload_target();
    while (g_ac97_producer_seq - g_ac97_current_seq < target) {
        if (!ac97_fill_descriptor(g_ac97_producer_seq)) break;
        g_ac97_producer_seq++;
        ac97_dma_publish();
        outb((uint16_t)(g_ac97_nabm + AC97_PO_LVI),
             (uint8_t)((g_ac97_producer_seq - 1U) &
                       (AC97_BDL_COUNT - 1U)));
    }
}

static void ac97_poll_stream(void) {
    uint16_t status;
    uint8_t civ;
    uint8_t delta;
    uint32_t timeout_ticks;

    if (!g_ac97_streaming) return;
    status = ac97_inw((uint16_t)(g_ac97_nabm + g_ac97_po_sr));
    civ = inb((uint16_t)(g_ac97_nabm + AC97_PO_CIV)) & 0x1FU;
    if (status & AC97_SR_FIFOE) {
        g_ac97_fifo_errors++;
        if (g_ac97_fifo_errors <= 4U)
            kprintf("[AC97] FIFO underrun #%u SR=%x CIV=%u LVI=%u\n",
                    g_ac97_fifo_errors, status, (uint32_t)civ,
                    (uint32_t)inb((uint16_t)(g_ac97_nabm + AC97_PO_LVI)));
    }
    if (!(status & AC97_SR_DCH)) g_ac97_observed_running = true;
    delta = (uint8_t)((civ - g_ac97_last_civ) & 0x1FU);
    if (delta && delta < 16U) {
        g_ac97_current_seq += delta;
        g_ac97_last_civ = civ;
    }
    if (status & AC97_SR_CLEAR)
        ac97_outw((uint16_t)(g_ac97_nabm + g_ac97_po_sr),
                  (uint16_t)(status & AC97_SR_CLEAR));

    if ((status & AC97_SR_DCH) && g_ac97_observed_running) {
        if (g_ac97_direct_active) {
            int16_t *finished = g_ac97_direct_buffer;
            g_ac97_direct_buffer = NULL;
            g_ac97_direct_active = false;
            g_ac97_streaming = false;
            ac97_prepare_ring_bdl();
            if (finished) kfree(finished);
            return;
        }
        if (ac97_has_active_voice()) {
            g_ac97_dch_restarts++;
            if (g_ac97_dch_restarts <= 4U)
                kprintf("[AC97] DMA alcanzo LVI antes de tiempo #%u CIV=%u LVI=%u\n",
                        g_ac97_dch_restarts, (uint32_t)civ,
                        (uint32_t)inb((uint16_t)(g_ac97_nabm + AC97_PO_LVI)));
        }
        g_ac97_current_seq = g_ac97_producer_seq;
        g_ac97_streaming = false;
        return;
    }
    if (!g_ac97_direct_active && delta && delta < 16U)
        ac97_fill_ahead();
    timeout_ticks = pit_get_frequency_hz();
    if (!timeout_ticks) timeout_ticks = 100U;
    if (!g_ac97_observed_running &&
        (uint32_t)(pit_get_ticks() - g_ac97_start_tick) > timeout_ticks) {
        kprintf("[AC97] DMA no inicio, SR=%x CR=%x\n", status,
                inb((uint16_t)(g_ac97_nabm + AC97_PO_CR)));
        ac97_channel_reset();
        ac97_clear_voices();
        if (g_ac97_direct_buffer) kfree(g_ac97_direct_buffer);
        g_ac97_direct_buffer = NULL;
        g_ac97_direct_active = false;
        ac97_prepare_ring_bdl();
        g_ac97_streaming = false;
    }
}

static void ac97_worker(void *argument UNUSED) {
    while (true) {
        if (g_ac97_present) {
            ac97_poll_stream();
            if (!g_ac97_streaming && ac97_has_active_voice())
                (void)ac97_start_stream();
        }
        task_sleep(1);
    }
}

static void ac97_pc_play(uint32_t frequency_hz) {
    uint32_t divisor;
    uint8_t speaker;
    if (!frequency_hz) return;
    divisor = 1193182U / frequency_hz;
    if (!divisor || divisor > 0xFFFFU) return;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(divisor & 0xFFU));
    outb(0x42, (uint8_t)(divisor >> 8));
    speaker = inb(0x61);
    outb(0x61, (uint8_t)(speaker | 0x03U));
}

static void ac97_stop(void) {
    int16_t *direct = g_ac97_direct_buffer;
    uint8_t speaker = inb(0x61);
    outb(0x61, (uint8_t)(speaker & 0xFCU));
    ac97_clear_voices();
    if (g_ac97_present) ac97_channel_reset();
    g_ac97_streaming = false;
    g_ac97_direct_buffer = NULL;
    g_ac97_direct_active = false;
    ac97_prepare_ring_bdl();
    if (direct) kfree(direct);
}

static bool ac97_play_pcm_u8(const uint8_t *samples, uint32_t length,
                             uint16_t rate, uint8_t volume) {
    if (!g_ac97_present) return false;
    if (ac97_start_direct(samples, length, rate, volume)) return true;
    if (g_ac97_direct_active) return false;
    return ac97_add_voice(samples, length, rate, volume);
}

static bool ac97_start_tone(uint32_t frequency_hz, uint32_t duration_ms) {
    uint32_t samples;
    uint32_t period;
    if (!frequency_hz || !duration_ms) return false;
    if (duration_ms > AC97_TONE_MAX_MS) duration_ms = AC97_TONE_MAX_MS;
    samples = (AC97_OUTPUT_HZ * duration_ms) / 1000U;
    period = AC97_OUTPUT_HZ / frequency_hz;
    if (period < 2U) period = 2U;
    ac97_stop();
    for (uint32_t i = 0; i < samples; i++)
        g_ac97_tone[i] = (i % period) < (period / 2U) ? 224U : 32U;
    return ac97_add_voice(g_ac97_tone, samples, AC97_OUTPUT_HZ, 200U);
}

static void ac97_beep(uint32_t frequency_hz, uint32_t duration_ms) {
    if (!duration_ms) {
        ac97_pc_play(frequency_hz);
        return;
    }
    if (!ac97_start_tone(frequency_hz, duration_ms)) return;
    while (g_ac97_streaming || ac97_has_active_voice()) task_sleep(1);
}

/* sound_poll() corre desde la IRQ del PIT; el trabajo pesado vive en worker. */
static void ac97_poll(void) {}
static bool ac97_pcm_available(void) { return g_ac97_present; }
static bool ac97_pcm_busy(void) {
    return g_ac97_present &&
           (g_ac97_streaming || ac97_has_active_voice());
}
static bool ac97_has_sb16(void) { return false; }
static bool ac97_sb16_tone(uint32_t hz UNUSED, uint32_t ms UNUSED) {
    return false;
}
static bool ac97_sb16_busy(void) { return false; }
static const char *ac97_pcm_name(void) {
    return g_ac97_controller ? g_ac97_controller->name
                             : "AC'97 ICH-compatible";
}

static bool ac97_codec_ready(void) {
    uint32_t ready = 0;
    for (uint32_t i = 0; i < 200000U; i++) {
        ready = inl((uint16_t)(g_ac97_nabm + AC97_GLOB_STA));
        if (ready & AC97_GLOB_PCR) return true;
        ac97_io_wait();
    }
    return false;
}

static bool ac97_init(void) {
    uint32_t nam_bar;
    uint32_t nabm_bar;
    uint16_t vendor1;
    uint16_t vendor2;
    static const sound_driver_ops_t ops = {
        ac97_poll,
        ac97_pc_play,
        ac97_stop,
        ac97_beep,
        ac97_start_tone,
        ac97_play_pcm_u8,
        ac97_pcm_available,
        ac97_pcm_busy,
        ac97_has_sb16,
        ac97_sb16_tone,
        ac97_sb16_busy,
        ac97_pcm_name,
        200U
    };

    g_ac97_pci = ac97_find_device();
    if (!g_ac97_pci) {
        kprintf("[AC97] controlador ICH-compatible no detectado\n");
        return false;
    }
    g_ac97_po_sr = g_ac97_controller->type == AC97_CONTROLLER_SIS7012
                 ? AC97_PO_SIS_SR : AC97_PO_SR;
    nam_bar = g_ac97_pci->bars[0];
    nabm_bar = g_ac97_pci->bars[1];
    if (!(nam_bar & 1U) || !(nabm_bar & 1U) ||
        !(nam_bar & 0xFFFFFFFCU) || !(nabm_bar & 0xFFFFFFFCU) ||
        (nam_bar & 0xFFFFFFFCU) > 0xFFFFU ||
        (nabm_bar & 0xFFFFFFFCU) > 0xFFFFU) {
        kprintf("[AC97] BAR0/BAR1 de E/S invalidas\n");
        return false;
    }
    g_ac97_nam = (uint16_t)(nam_bar & 0xFFFFFFFCU);
    g_ac97_nabm = (uint16_t)(nabm_bar & 0xFFFFFFFCU);
    if (!pci_enable_command(g_ac97_pci,
            PCI_COMMAND_IO | PCI_COMMAND_BUSMASTER)) return false;

    outl((uint16_t)(g_ac97_nabm + AC97_GLOB_CNT), AC97_GLOB_COLD);
    if (!ac97_codec_ready()) {
        kprintf("[AC97] codec primario no responde, GLOB_STA=%x\n",
                inl((uint16_t)(g_ac97_nabm + AC97_GLOB_STA)));
        return false;
    }
    ac97_outw((uint16_t)(g_ac97_nam + AC97_NAM_RESET), 0);
    ac97_outw((uint16_t)(g_ac97_nam + AC97_NAM_MASTER_VOL), 0);
    ac97_outw((uint16_t)(g_ac97_nam + AC97_NAM_PCM_VOL), 0);
    if (g_ac97_controller->type == AC97_CONTROLLER_SIS7012) {
        /* SiS intercambia SR/PICB y requiere habilitar la salida PCM. */
        uint16_t sis_output = ac97_inw((uint16_t)(g_ac97_nabm + 0x4CU));
        ac97_outw((uint16_t)(g_ac97_nabm + 0x4CU),
                  (uint16_t)(sis_output | 1U));
    }
    vendor1 = ac97_inw((uint16_t)(g_ac97_nam + AC97_NAM_VENDOR1));
    vendor2 = ac97_inw((uint16_t)(g_ac97_nam + AC97_NAM_VENDOR2));
    if ((vendor1 == 0xFFFFU && vendor2 == 0xFFFFU) ||
        (!vendor1 && !vendor2)) {
        kprintf("[AC97] codec presente pero ID invalido\n");
        return false;
    }

    kmemset(g_ac97_buffers, 0, sizeof(g_ac97_buffers));
    ac97_prepare_ring_bdl();
    ac97_clear_voices();
    g_ac97_streaming = false;
    g_ac97_observed_running = false;
    g_ac97_direct_buffer = NULL;
    g_ac97_direct_active = false;
    g_ac97_fifo_errors = 0;
    g_ac97_dch_restarts = 0;
    ac97_channel_reset();
    g_ac97_present = true;
    if (task_create("ac97-mixer", ac97_worker, NULL) < 0) {
        g_ac97_present = false;
        return false;
    }
    if (!sound_register_driver(&ops)) {
        g_ac97_present = false;
        return false;
    }
    kprintf("[AC97] %s %x:%x NAM=%x NABM=%x codec=%x:%x\n",
            g_ac97_controller->name,
            g_ac97_pci->vendor_id, g_ac97_pci->device_id,
            g_ac97_nam, g_ac97_nabm, vendor1, vendor2);
    kprintf("[AC97] PCM 48000 Hz S16LE stereo, BDL=%x buffers=%u x %u frames\n",
            (uint32_t)(uintptr_t)g_ac97_bdl, AC97_BDL_COUNT,
            AC97_BUFFER_FRAMES);
    return true;
}

static void ac97_shutdown(void) {
    ac97_stop();
    g_ac97_present = false;
}

const bk_driver_module_t *bleskernos_driver_query(void) {
    static const bk_driver_module_t module = {
        BK_DRIVER_ABI_VERSION,
        sizeof(bk_driver_module_t),
        "ac97",
        "Intel/AMD/NVIDIA/SiS AC'97 PCM bus-master",
        ac97_init,
        ac97_shutdown
    };
    return &module;
}
