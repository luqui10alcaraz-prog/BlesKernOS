// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ESS Allegro / Maestro3 external driver for BlesKernOS.
 *
 * Hardware sequencing and DSP layout are derived from the Linux and FreeBSD
 * Maestro3 drivers.  Copyright (c) 2000 Zach Brown, Takashi Iwai; copyright
 * (c) 2001 Scott Long and Darrell Anderson.  The ESS register description
 * and firmware originate from Don Kim's GPL driver.
 */
#include "../include/driver.h"
#include "../include/pci.h"
#include "../include/pit.h"
#include "../include/sound.h"
#include "../include/task.h"
#include "../include/vga.h"
#include "maestro3_regs.h"
#include "maestro3_firmware.h"

#define M3_VENDOR_ESS          0x125DU
#define M3_RING_SIZE           4096U
#define M3_RING_MASK           (M3_RING_SIZE - 1U)
#define M3_OUTPUT_RATE         48000U
#define M3_VOICE_COUNT         8U
#define M3_TONE_MAX_MS         1000U
#define M3_DAC_DATA            0x1100U
#define M3_STALL_MS            750U

typedef struct {
    const uint8_t *samples;
    uint32_t length;
    uint64_t position_fp;
    uint32_t step_fp;
    uint8_t volume;
    bool active;
} m3_voice_t;

typedef struct {
    const pci_device_t *pci;
    uint16_t io;
    uint16_t device_id;
    uint16_t subsystem_vendor;
    uint16_t subsystem_device;
    uint8_t reset_state;
    bool allegro;
    bool irda_workaround;
    bool present;
    bool streaming;
    uint32_t last_hw;
    uint32_t silent_refill;
    uint32_t last_progress_tick;
    uint32_t stalls;
} m3_state_t;

typedef struct { uint16_t offset, value; } m3_init_word_t;

static m3_state_t g_m3;
static m3_voice_t g_m3_voices[M3_VOICE_COUNT];
static uint8_t g_m3_ring[M3_RING_SIZE] __attribute__((aligned(64)));
static uint8_t g_m3_tone[M3_OUTPUT_RATE] __attribute__((aligned(16)));

static const uint16_t g_m3_devices[] = {
    0x1988U, 0x1989U, 0x1990U, 0x1992U,
    0x1998U, 0x1999U, 0x199AU, 0x199BU
};

static const uint16_t g_m3_lpf[] = {
    0x0743U, 0x1104U, 0x0A4CU, 0xF88DU, 0x242CU,
    0x1023U, 0x1AA9U, 0x0B60U, 0xEFDDU, 0x186FU
};

static const m3_init_word_t g_m3_play_init[] = {
    {M3_CDATA_LEFT_VOLUME, M3_ARB_VOLUME},
    {M3_CDATA_RIGHT_VOLUME, M3_ARB_VOLUME},
    {M3_SRC3_DIRECTION, 0},
    {M3_SRC3_DIRECTION + 3U, 0},
    {M3_SRC3_DIRECTION + 4U, 0},
    {M3_SRC3_DIRECTION + 5U, 0},
    {M3_SRC3_DIRECTION + 6U, 0},
    {M3_SRC3_DIRECTION + 7U, 0},
    {M3_SRC3_DIRECTION + 8U, 0},
    {M3_SRC3_DIRECTION + 9U, 0},
    {M3_SRC3_DIRECTION + 10U, 0x8000U},
    {M3_SRC3_DIRECTION + 11U, 0xFF00U},
    {M3_SRC3_DIRECTION + 13U, 0},
    {M3_SRC3_DIRECTION + 14U, 0},
    {M3_SRC3_DIRECTION + 15U, 0},
    {M3_SRC3_DIRECTION + 16U, 8U},
    {M3_SRC3_DIRECTION + 17U, 100U},
    {M3_SRC3_DIRECTION + 18U, M3_MINISRC_BIQUAD_STAGE - 1U},
    {M3_SRC3_DIRECTION + 20U, 0},
    {M3_SRC3_DIRECTION + 21U, 0}
};

static inline uint8_t m3_inb(uint16_t reg) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"((uint16_t)(g_m3.io + reg)));
    return value;
}

static inline uint16_t m3_inw(uint16_t reg) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"((uint16_t)(g_m3.io + reg)));
    return value;
}

static inline void m3_outb(uint16_t reg, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"((uint16_t)(g_m3.io + reg)));
}

static inline void m3_outw(uint16_t reg, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"((uint16_t)(g_m3.io + reg)));
}

static inline void m3_io_wait(void) {
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}

static void m3_delay_us(uint32_t microseconds) {
    /* Puerto 0x80 tarda aproximadamente un ciclo ISA; el margen amplio es
       intencional para estos codecs de portatiles antiguos. */
    uint32_t loops = microseconds * 2U + 1U;
    while (loops--) m3_io_wait();
}

static void m3_delay_ms(uint32_t milliseconds) {
    uint32_t hz = pit_get_frequency_hz();
    uint32_t ticks;
    if (!milliseconds) return;
    if (!hz) hz = 100U;
    ticks = (milliseconds * hz + 999U) / 1000U;
    if (!ticks) ticks = 1U;
    task_sleep(ticks);
}

static uint32_t m3_cfg32(uint8_t offset) {
    return pci_config_read32(g_m3.pci->bus, g_m3.pci->slot,
                             g_m3.pci->function, offset);
}

static uint16_t m3_cfg16(uint8_t offset) {
    return pci_config_read16(g_m3.pci->bus, g_m3.pci->slot,
                             g_m3.pci->function, offset);
}

static void m3_cfg_write32(uint8_t offset, uint32_t value) {
    pci_config_write32(g_m3.pci->bus, g_m3.pci->slot,
                       g_m3.pci->function, offset, value);
}

static void m3_cfg_write16(uint8_t offset, uint16_t value) {
    pci_config_write16(g_m3.pci->bus, g_m3.pci->slot,
                       g_m3.pci->function, offset, value);
}

static uint16_t m3_assp_read(uint16_t region, uint16_t index) {
    m3_outw(M3_DSP_MEMORY_TYPE, region & M3_MEMTYPE_MASK);
    m3_outw(M3_DSP_MEMORY_INDEX, index);
    return m3_inw(M3_DSP_MEMORY_DATA);
}

static void m3_assp_write(uint16_t region, uint16_t index, uint16_t value) {
    m3_outw(M3_DSP_MEMORY_TYPE, region & M3_MEMTYPE_MASK);
    m3_outw(M3_DSP_MEMORY_INDEX, index);
    m3_outw(M3_DSP_MEMORY_DATA, value);
}

static uint16_t m3_fw_word(const uint8_t *firmware, uint32_t byte_offset) {
    return (uint16_t)firmware[byte_offset] |
           (uint16_t)((uint16_t)firmware[byte_offset + 1U] << 8);
}

static bool m3_ac97_wait(void) {
    for (uint32_t i = 0; i < 100000U; i++) {
        if (!(m3_inb(M3_CODEC_COMMAND) & 1U)) return true;
        m3_io_wait();
    }
    return false;
}

static uint16_t m3_ac97_read(uint8_t reg) {
    if (!m3_ac97_wait()) return 0xFFFFU;
    m3_outb(M3_CODEC_COMMAND, (uint8_t)(0x80U | (reg & 0x7FU)));
    m3_delay_us(22U);
    if (!m3_ac97_wait()) return 0xFFFFU;
    return m3_inw(M3_CODEC_DATA);
}

static bool m3_ac97_write(uint8_t reg, uint16_t value) {
    if (!m3_ac97_wait()) return false;
    m3_outw(M3_CODEC_DATA, value);
    m3_outb(M3_CODEC_COMMAND, reg & 0x7FU);
    return m3_ac97_wait();
}

static void m3_assp_firmware_init(void) {
    uint32_t words;
    for (uint32_t i = 0;
         i < (M3_REV_B_DATA_UNIT_LEN * M3_NUM_KERNEL_DATA_UNITS) / 2U; i++) {
        m3_assp_write(M3_MEM_INTERNAL_DATA, (uint16_t)(M3_KDATA_BASE + i), 0);
        m3_assp_write(M3_MEM_INTERNAL_DATA, (uint16_t)(M3_KDATA_BASE2 + i), 0);
    }
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_KDATA_CURRENT_DMA,
                  M3_KDATA_DMA_XFER0);

    words = m3_assp_kernel_firmware_len / 2U;
    for (uint32_t i = 0; i < words; i++)
        m3_assp_write(M3_MEM_INTERNAL_CODE, (uint16_t)i,
                      m3_fw_word(m3_assp_kernel_firmware, i * 2U));

    words = m3_assp_minisrc_firmware_len / 2U;
    for (uint32_t i = 0; i < words; i++)
        m3_assp_write(M3_MEM_INTERNAL_CODE, (uint16_t)(0x400U + i),
                      m3_fw_word(m3_assp_minisrc_firmware, i * 2U));

    for (uint32_t i = 0; i < sizeof(g_m3_lpf) / sizeof(g_m3_lpf[0]); i++)
        m3_assp_write(M3_MEM_INTERNAL_CODE,
                      (uint16_t)(0x400U + M3_MINISRC_COEF_LOC + i),
                      g_m3_lpf[i]);
    m3_assp_write(M3_MEM_INTERNAL_CODE,
                  0x400U + M3_MINISRC_COEF_LOC +
                  sizeof(g_m3_lpf) / sizeof(g_m3_lpf[0]), 0x8000U);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_KDATA_TASK0, 0x400U);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_KDATA_MIXER_TASKS, 0);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_KDATA_DAC_LEFT_VOLUME,
                  M3_ARB_VOLUME);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_KDATA_DAC_RIGHT_VOLUME,
                  M3_ARB_VOLUME);
}

static void m3_chip_configure(void) {
    uint16_t legacy = m3_cfg16(M3_PCI_LEGACY_AUDIO_CTRL);
    uint32_t config;
    uint8_t clock;
    legacy &= (uint16_t)~(M3_SOUND_BLASTER_ENABLE | M3_FM_SYNTHESIS_ENABLE |
                          M3_MPU401_IO_ENABLE | M3_MPU401_IRQ_ENABLE |
                          M3_ALIAS_10BIT_IO);
    m3_cfg_write16(M3_PCI_LEGACY_AUDIO_CTRL, legacy);

    config = m3_cfg32(M3_PCI_ALLEGRO_CONFIG);
    config |= M3_REDUCED_DEBOUNCE | M3_PM_CTRL_ENABLE |
              M3_CLK_DIV_BY_49 | M3_USE_PCI_TIMING;
    m3_cfg_write32(M3_PCI_ALLEGRO_CONFIG, config);

    m3_outb(M3_ASSP_CONTROL_B, M3_RESET_ASSP);
    config = m3_cfg32(M3_PCI_ALLEGRO_CONFIG);
    config &= ~M3_INT_CLK_SELECT;
    if (!g_m3.allegro) {
        config &= ~M3_INT_CLK_MULT_ENABLE;
        config |= M3_INT_CLK_SRC_NOT_PCI;
    } else {
        uint32_t user = m3_cfg32(M3_PCI_USER_CONFIG);
        m3_cfg_write32(M3_PCI_USER_CONFIG, user | M3_IN_CLK_12MHZ_SELECT);
    }
    config &= ~(M3_CLK_MULT_MODE_SELECT | M3_CLK_MULT_MODE_SELECT2);
    m3_cfg_write32(M3_PCI_ALLEGRO_CONFIG, config);

    clock = m3_inb(M3_ASSP_CONTROL_A);
    clock &= (uint8_t)~(M3_DSP_CLK_36MHZ_SELECT |
                        M3_ASSP_CLK_49MHZ_SELECT);
    clock |= M3_ASSP_CLK_49MHZ_SELECT | M3_ASSP_0_WS_ENABLE;
    m3_outb(M3_ASSP_CONTROL_A, clock);
    m3_assp_firmware_init();
    m3_outb(M3_ASSP_CONTROL_B, M3_RUN_ASSP);
    m3_outb(M3_HARDWARE_VOL_CTRL, 0);
    m3_outb(M3_SHADOW_MIX_REG_VOICE, 0x88U);
    m3_outb(M3_HW_VOL_COUNTER_VOICE, 0x88U);
    m3_outb(M3_SHADOW_MIX_REG_MASTER, 0x88U);
    m3_outb(M3_HW_VOL_COUNTER_MASTER, 0x88U);
}

static void m3_assp_halt(void) {
    g_m3.reset_state = m3_inb(M3_ASSP_CONTROL_B) &
                       (uint8_t)~M3_REGB_STOP_CLOCK;
    m3_delay_ms(10U);
    m3_outb(M3_ASSP_CONTROL_B,
            g_m3.reset_state & (uint8_t)~M3_REGB_ENABLE_RESET);
    m3_delay_ms(10U);
}

static void m3_assp_continue(void) {
    m3_outb(M3_ASSP_CONTROL_B,
            g_m3.reset_state | M3_REGB_ENABLE_RESET);
}

static bool m3_codec_reset(void) {
    uint32_t delay1 = g_m3.allegro ? 50U : 20U;
    uint32_t delay2 = g_m3.allegro ? 800U : 500U;
    for (uint32_t retry = 0; retry < 5U; retry++) {
        uint16_t dir = m3_inw(M3_GPIO_DIRECTION);
        uint16_t ring_b;
        uint16_t vendor;
        if (!g_m3.irda_workaround) dir |= 0x10U;
        ring_b = m3_inw(M3_RING_BUS_CTRL_B) &
                 (uint16_t)~M3_SECOND_CODEC_ID_MASK;
        /* Dell Latitude C810: habilita el codec del dock. */
        if (g_m3.subsystem_vendor == 0x1028U &&
            g_m3.subsystem_device == 0x00E5U)
            ring_b |= M3_M3I_DOCK_ENABLE;
        m3_outw(M3_RING_BUS_CTRL_B, ring_b);
        m3_outw(M3_SDO_OUT_DEST_CTRL,
                m3_inw(M3_SDO_OUT_DEST_CTRL) &
                (uint16_t)~M3_COMMAND_ADDR_OUT);
        m3_outw(M3_SDO_IN_DEST_CTRL,
                m3_inw(M3_SDO_IN_DEST_CTRL) &
                (uint16_t)~M3_STATUS_ADDR_IN);

        m3_outw(M3_RING_BUS_CTRL_A, M3_IO_SRAM_ENABLE);
        m3_delay_us(20U);
        m3_outw(M3_GPIO_DIRECTION, dir & (uint16_t)~M3_GPO_PRIMARY_AC97);
        m3_outw(M3_GPIO_MASK, (uint16_t)~M3_GPO_PRIMARY_AC97);
        m3_outw(M3_GPIO_DATA, 0);
        m3_outw(M3_GPIO_DIRECTION, dir | M3_GPO_PRIMARY_AC97);
        m3_delay_ms(delay1);
        m3_outw(M3_GPIO_DATA, M3_GPO_PRIMARY_AC97);
        m3_delay_us(5U);
        m3_outw(M3_RING_BUS_CTRL_A,
                M3_IO_SRAM_ENABLE | M3_SERIAL_AC_LINK_ENABLE);
        m3_outw(M3_GPIO_MASK, 0xFFFFU);
        m3_delay_ms(delay2);
        vendor = m3_ac97_read(M3_AC97_VENDOR_ID1);
        if (vendor != 0U && vendor != 0xFFFFU) return true;
        delay1 += 10U;
        delay2 += 100U;
    }
    return false;
}

static void m3_amp_enable(bool enabled) {
    uint16_t pin = (uint16_t)(1U << (g_m3.allegro ? 8U : 1U));
    uint16_t polarity = enabled ? 0U : pin;
    m3_outw(M3_GPIO_MASK, (uint16_t)~pin);
    m3_outw(M3_GPIO_DIRECTION, m3_inw(M3_GPIO_DIRECTION) | pin);
    m3_outw(M3_GPIO_DATA,
            M3_GPO_SECONDARY_AC97 | M3_GPO_PRIMARY_AC97 | polarity);
    m3_outw(M3_GPIO_MASK, 0xFFFFU);
}

static void m3_setup_playback(void) {
    uint32_t dma = (uint32_t)(uintptr_t)g_m3_ring;
    uint32_t end = dma + M3_RING_SIZE;
    uint32_t dsp_in_size = M3_MINISRC_IN_SIZE - 0x40U;
    uint32_t dsp_out_size = M3_MINISRC_OUT_SIZE - 0x40U;
    uint32_t dsp_in = M3_DAC_DATA + M3_MINISRC_TMP_SIZE / 2U;
    uint32_t dsp_out = dsp_in + dsp_in_size / 2U + 1U;

    for (uint32_t i = M3_DAC_DATA; i < 0x1C00U; i++)
        m3_assp_write(M3_MEM_INTERNAL_DATA, (uint16_t)i, 0);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_CDATA_HOST_ADDR_LO,
                  (uint16_t)dma);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_CDATA_HOST_ADDR_HI,
                  (uint16_t)(dma >> 16));
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_CDATA_HOST_END_LO,
                  (uint16_t)end);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_CDATA_HOST_END_HI,
                  (uint16_t)(end >> 16));
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_CDATA_HOST_CUR_LO,
                  (uint16_t)dma);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_CDATA_HOST_CUR_HI,
                  (uint16_t)(dma >> 16));

    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_CDATA_IN_BEGIN,
                  (uint16_t)dsp_in);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_CDATA_IN_END,
                  (uint16_t)(dsp_in + dsp_in_size / 2U));
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_CDATA_IN_HEAD,
                  (uint16_t)dsp_in);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_CDATA_IN_TAIL,
                  (uint16_t)dsp_in);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_CDATA_OUT_BEGIN,
                  (uint16_t)dsp_out);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_CDATA_OUT_END,
                  (uint16_t)(dsp_out + dsp_out_size / 2U));
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_CDATA_OUT_HEAD,
                  (uint16_t)dsp_out);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_CDATA_OUT_TAIL,
                  (uint16_t)dsp_out);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_SRC3_DIRECTION + 12U,
                  M3_DAC_DATA + 48U);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_SRC3_DIRECTION + 19U,
                  0x400U + M3_MINISRC_COEF_LOC);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_SRC3_DIRECTION + 22U,
                  0xFFU);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_CDATA_DMA_CONTROL,
                  M3_DMAC_AUTOREPEAT | M3_DMAC_PAGE3 | M3_DMAC_BLOCKF);
    for (uint32_t i = 0;
         i < sizeof(g_m3_play_init) / sizeof(g_m3_play_init[0]); i++)
        m3_assp_write(M3_MEM_INTERNAL_DATA,
                      (uint16_t)(M3_DAC_DATA + g_m3_play_init[i].offset),
                      g_m3_play_init[i].value);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_SRC3_MODE, 1U);
    m3_assp_write(M3_MEM_INTERNAL_DATA,
                  M3_DAC_DATA + M3_SRC3_WORD_LENGTH, 1U);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_CDATA_FREQUENCY,
                  0x7FFFU);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_KDATA_INSTANCE0_MINISRC,
                  M3_DAC_DATA >> M3_DP_SHIFT_COUNT);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_KDATA_DMA_XFER0,
                  M3_DAC_DATA >> M3_DP_SHIFT_COUNT);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_KDATA_MIXER_XFER0,
                  M3_DAC_DATA >> M3_DP_SHIFT_COUNT);
}

static bool m3_any_voice(void) {
    for (uint32_t i = 0; i < M3_VOICE_COUNT; i++)
        if (g_m3_voices[i].active) return true;
    return false;
}

static uint8_t m3_mix_sample(void) {
    int mixed = 0;
    bool any = false;
    for (uint32_t i = 0; i < M3_VOICE_COUNT; i++) {
        m3_voice_t *voice = &g_m3_voices[i];
        uint32_t pos, next, fraction;
        int first, second, sample;
        if (!voice->active) continue;
        pos = (uint32_t)(voice->position_fp >> 16);
        if (pos >= voice->length) {
            voice->active = false;
            continue;
        }
        next = pos + 1U < voice->length ? pos + 1U : pos;
        fraction = (uint32_t)voice->position_fp & 0xFFFFU;
        first = (int)voice->samples[pos] - 128;
        second = (int)voice->samples[next] - 128;
        sample = first + ((second - first) * (int)fraction) / 65536;
        mixed += sample * (int)voice->volume / 255;
        voice->position_fp += voice->step_fp;
        if ((voice->position_fp >> 16) >= voice->length)
            voice->active = false;
        any = true;
    }
    if (!any) return 128U;
    if (mixed < -128) mixed = -128;
    if (mixed > 127) mixed = 127;
    return (uint8_t)(mixed + 128);
}

static void m3_render(uint32_t offset, uint32_t count) {
    for (uint32_t i = 0; i < count; i++)
        g_m3_ring[(offset + i) & M3_RING_MASK] = m3_mix_sample();
    __asm__ volatile ("" : : : "memory");
}

static uint32_t m3_hardware_offset(void) {
    uint16_t high1, high2, low;
    uint32_t address;
    do {
        high1 = m3_assp_read(M3_MEM_INTERNAL_DATA,
                             M3_DAC_DATA + M3_CDATA_HOST_CUR_HI);
        low = m3_assp_read(M3_MEM_INTERNAL_DATA,
                           M3_DAC_DATA + M3_CDATA_HOST_CUR_LO);
        high2 = m3_assp_read(M3_MEM_INTERNAL_DATA,
                             M3_DAC_DATA + M3_CDATA_HOST_CUR_HI);
    } while (high1 != high2);
    address = (uint32_t)low | ((uint32_t)high2 << 16);
    if (address < (uint32_t)(uintptr_t)g_m3_ring ||
        address >= (uint32_t)(uintptr_t)g_m3_ring + M3_RING_SIZE)
        return M3_RING_SIZE;
    return address - (uint32_t)(uintptr_t)g_m3_ring;
}

static void m3_stream_start(void) {
    m3_render(0, M3_RING_SIZE);
    g_m3.last_hw = 0;
    g_m3.silent_refill = m3_any_voice() ? 0U : M3_RING_SIZE;
    g_m3.last_progress_tick = pit_get_ticks();
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_KDATA_TIMER_RELOAD, 240U);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_KDATA_TIMER_CURRENT, 240U);
    m3_outw(M3_HOST_INT_CTRL,
            m3_inw(M3_HOST_INT_CTRL) | M3_CLKRUN_GEN_ENABLE);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_DAC_DATA + M3_CDATA_READY, 1U);
    m3_assp_write(M3_MEM_INTERNAL_DATA, M3_KDATA_MIXER_TASKS, 1U);
    g_m3.streaming = true;
}

static void m3_stream_stop(void) {
    if (g_m3.io) {
        m3_assp_write(M3_MEM_INTERNAL_DATA,
                      M3_DAC_DATA + M3_CDATA_READY, 0);
        m3_assp_write(M3_MEM_INTERNAL_DATA, M3_KDATA_MIXER_TASKS, 0);
        m3_assp_write(M3_MEM_INTERNAL_DATA, M3_KDATA_TIMER_RELOAD, 0);
        m3_assp_write(M3_MEM_INTERNAL_DATA, M3_KDATA_TIMER_CURRENT, 0);
        m3_outw(M3_HOST_INT_CTRL,
                m3_inw(M3_HOST_INT_CTRL) &
                (uint16_t)~M3_CLKRUN_GEN_ENABLE);
    }
    g_m3.streaming = false;
    g_m3.silent_refill = 0;
}

static void m3_worker(void *argument UNUSED) {
    while (true) {
        if (g_m3.present) {
            task_preempt_disable();
            if (!g_m3.streaming && m3_any_voice()) {
                m3_stream_start();
            } else if (g_m3.streaming) {
                uint32_t hw = m3_hardware_offset();
                if (hw >= M3_RING_SIZE) {
                    kprintf("[M3] puntero DMA invalido; deteniendo PCM\n");
                    m3_stream_stop();
                } else {
                    uint32_t consumed = (hw - g_m3.last_hw) & M3_RING_MASK;
                    if (consumed) {
                        bool had_voice = m3_any_voice();
                        m3_render(g_m3.last_hw, consumed);
                        g_m3.last_hw = hw;
                        g_m3.last_progress_tick = pit_get_ticks();
                        if (had_voice || m3_any_voice()) g_m3.silent_refill = 0;
                        else g_m3.silent_refill += consumed;
                        if (g_m3.silent_refill >= M3_RING_SIZE)
                            m3_stream_stop();
                    } else {
                        uint32_t elapsed = pit_get_ticks() - g_m3.last_progress_tick;
                        uint32_t hz = pit_get_frequency_hz();
                        if (!hz) hz = 100U;
                        if (elapsed >= (M3_STALL_MS * hz + 999U) / 1000U) {
                            g_m3.stalls++;
                            kprintf("[M3] DMA sin progreso (%u); reinicio de canal\n",
                                    g_m3.stalls);
                            m3_stream_stop();
                        }
                    }
                }
            }
            task_preempt_enable();
        }
        task_sleep(1U);
    }
}

static void m3_clear_voices(void) {
    for (uint32_t i = 0; i < M3_VOICE_COUNT; i++) {
        g_m3_voices[i].samples = NULL;
        g_m3_voices[i].length = 0;
        g_m3_voices[i].position_fp = 0;
        g_m3_voices[i].step_fp = 0;
        g_m3_voices[i].volume = 0;
        g_m3_voices[i].active = false;
    }
}

static bool m3_add_voice(const uint8_t *samples, uint32_t length,
                         uint16_t rate, uint8_t volume) {
    uint32_t slot = M3_VOICE_COUNT;
    if (!g_m3.present || !samples || !length || rate < 4000U ||
        rate > 48000U) return false;
    task_preempt_disable();
    for (uint32_t i = 0; i < M3_VOICE_COUNT; i++) {
        if (!g_m3_voices[i].active) { slot = i; break; }
    }
    if (slot < M3_VOICE_COUNT) {
        m3_voice_t *voice = &g_m3_voices[slot];
        voice->samples = samples;
        voice->length = length;
        voice->position_fp = 0;
        voice->step_fp = ((uint32_t)rate << 16) / M3_OUTPUT_RATE;
        if (!voice->step_fp) voice->step_fp = 1U;
        voice->volume = volume;
        voice->active = true;
    }
    task_preempt_enable();
    return slot < M3_VOICE_COUNT;
}

static void m3_pc_play(uint32_t frequency_hz) {
    uint32_t divisor;
    uint8_t speaker;
    if (!frequency_hz) return;
    divisor = 1193182U / frequency_hz;
    if (!divisor || divisor > 0xFFFFU) return;
    __asm__ volatile ("outb %0, $0x43" : : "a"((uint8_t)0xB6));
    __asm__ volatile ("outb %0, $0x42" : : "a"((uint8_t)divisor));
    __asm__ volatile ("outb %0, $0x42" : : "a"((uint8_t)(divisor >> 8)));
    __asm__ volatile ("inb $0x61, %0" : "=a"(speaker));
    speaker |= 3U;
    __asm__ volatile ("outb %0, $0x61" : : "a"(speaker));
}

static void m3_stop(void) {
    uint8_t speaker;
    __asm__ volatile ("inb $0x61, %0" : "=a"(speaker));
    speaker &= 0xFCU;
    __asm__ volatile ("outb %0, $0x61" : : "a"(speaker));
    task_preempt_disable();
    m3_clear_voices();
    if (g_m3.streaming) m3_stream_stop();
    task_preempt_enable();
}

static bool m3_start_tone(uint32_t frequency_hz, uint32_t duration_ms) {
    uint32_t count, period;
    if (!frequency_hz || !duration_ms || !g_m3.present) return false;
    if (duration_ms > M3_TONE_MAX_MS) duration_ms = M3_TONE_MAX_MS;
    count = M3_OUTPUT_RATE * duration_ms / 1000U;
    period = M3_OUTPUT_RATE / frequency_hz;
    if (period < 2U) period = 2U;
    for (uint32_t i = 0; i < count; i++)
        g_m3_tone[i] = i % period < period / 2U ? 224U : 32U;
    return m3_add_voice(g_m3_tone, count, M3_OUTPUT_RATE, 200U);
}

static void m3_beep(uint32_t frequency_hz, uint32_t duration_ms) {
    if (!duration_ms || !m3_start_tone(frequency_hz, duration_ms)) {
        m3_pc_play(frequency_hz);
        if (duration_ms) { m3_delay_ms(duration_ms); m3_stop(); }
        return;
    }
    while (g_m3.streaming || m3_any_voice()) task_sleep(1U);
}

static void m3_poll(void) {}
static bool m3_pcm_available(void) { return g_m3.present; }
static bool m3_pcm_busy(void) {
    return g_m3.present && (g_m3.streaming || m3_any_voice());
}
static bool m3_has_sb16(void) { return false; }
static bool m3_sb16_tone(uint32_t hz UNUSED, uint32_t ms UNUSED) {
    return false;
}
static bool m3_sb16_busy(void) { return false; }
static const char *m3_pcm_name(void) {
    return g_m3.allegro ? "ESS Allegro" : "ESS Maestro3";
}

static bool m3_match(uint16_t device) {
    for (uint32_t i = 0; i < sizeof(g_m3_devices) / sizeof(g_m3_devices[0]); i++)
        if (g_m3_devices[i] == device) return true;
    return false;
}

static const pci_device_t *m3_find(void) {
    for (uint32_t i = 0; i < pci_device_count(); i++) {
        const pci_device_t *device = pci_device_at(i);
        if (device && device->vendor_id == M3_VENDOR_ESS &&
            m3_match(device->device_id)) return device;
    }
    return NULL;
}

static bool m3_driver_init(void) {
    static const sound_driver_ops_t ops = {
        m3_poll, m3_pc_play, m3_stop, m3_beep, m3_start_tone, m3_add_voice,
        m3_pcm_available, m3_pcm_busy, m3_has_sb16, m3_sb16_tone,
        m3_sb16_busy, m3_pcm_name, 300U
    };
    uint32_t bar;
    uint16_t codec1, codec2;

    g_m3.pci = m3_find();
    if (!g_m3.pci) return false;
    g_m3.device_id = g_m3.pci->device_id;
    g_m3.allegro = g_m3.device_id == 0x1988U ||
                   g_m3.device_id == 0x1989U ||
                   g_m3.device_id == 0x1990U ||
                   g_m3.device_id == 0x1992U;
    g_m3.subsystem_vendor = m3_cfg16(0x2CU);
    g_m3.subsystem_device = m3_cfg16(0x2EU);
    g_m3.irda_workaround = g_m3.subsystem_vendor == 0x1028U &&
        (g_m3.subsystem_device == 0x00B0U ||
         g_m3.subsystem_device == 0x00A4U ||
         g_m3.subsystem_device == 0x00E6U);

    bar = g_m3.pci->bars[0];
    if (!(bar & 1U) || !(bar & 0xFFFFFFFCU) ||
        (bar & 0xFFFFFFFCU) > 0xFF00U) {
        kprintf("[M3] BAR0 de E/S invalida: %x\n", bar);
        return false;
    }
    g_m3.io = (uint16_t)(bar & 0xFFFFFFFCU);
    if ((uint32_t)(uintptr_t)g_m3_ring >= 0x10000000U ||
        (uint32_t)(uintptr_t)g_m3_ring + M3_RING_SIZE >= 0x10000000U) {
        kprintf("[M3] buffer DMA fuera del limite de 28 bits: %x\n",
                (uint32_t)(uintptr_t)g_m3_ring);
        return false;
    }
    if (!pci_enable_command(g_m3.pci,
                            PCI_COMMAND_IO | PCI_COMMAND_BUSMASTER))
        return false;

    m3_outw(M3_HOST_INT_CTRL, 0);
    m3_chip_configure();
    m3_assp_halt();
    if (!m3_codec_reset()) {
        kprintf("[M3] el codec AC97 no respondio despues de 5 intentos\n");
        return false;
    }
    m3_amp_enable(true);
    (void)m3_ac97_write(M3_AC97_MASTER_VOLUME, 0);
    (void)m3_ac97_write(M3_AC97_PCM_VOLUME, 0);
    codec1 = m3_ac97_read(M3_AC97_VENDOR_ID1);
    codec2 = m3_ac97_read(M3_AC97_VENDOR_ID2);
    m3_setup_playback();
    m3_assp_continue();

    m3_clear_voices();
    g_m3.streaming = false;
    g_m3.stalls = 0;
    g_m3.present = true;
    if (task_create("maestro3-mix", m3_worker, NULL) < 0) {
        g_m3.present = false;
        return false;
    }
    if (!sound_register_driver(&ops)) {
        g_m3.present = false;
        return false;
    }
    kprintf("[M3] ESS %x:%x SUBSYS=%x:%x IO=%x codec=%x:%x\n",
            M3_VENDOR_ESS, g_m3.device_id, g_m3.subsystem_vendor,
            g_m3.subsystem_device, g_m3.io, codec1, codec2);
    kprintf("[M3] DSP cargado (%u+%u bytes), PCM U8 mono 48000 Hz, DMA=%x\n",
            m3_assp_kernel_firmware_len, m3_assp_minisrc_firmware_len,
            (uint32_t)(uintptr_t)g_m3_ring);
    if (g_m3.irda_workaround)
        kprintf("[M3] quirk Dell Inspiron IrDA activado\n");
    return true;
}

static void m3_driver_shutdown(void) {
    m3_stop();
    if (g_m3.present) {
        m3_amp_enable(false);
        m3_outw(M3_HOST_INT_CTRL, 0);
    }
    g_m3.present = false;
}

const bk_driver_module_t *bleskernos_driver_query(void) {
    static const bk_driver_module_t module = {
        BK_DRIVER_ABI_VERSION,
        sizeof(bk_driver_module_t),
        "maestro3",
        "ESS Allegro/Maestro3 DSP audio (ES1988/ES199x)",
        m3_driver_init,
        m3_driver_shutdown
    };
    return &module;
}
