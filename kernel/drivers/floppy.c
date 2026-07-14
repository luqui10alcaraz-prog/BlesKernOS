#include "../include/floppy.h"
#include "../include/block.h"
#include "../include/memory.h"
#include "../include/pic.h"
#include "../include/vga.h"
#include "../include/bootsplash.h"

/*
 * BlesKernOS floppy driver - DMA based reader.
 *
 * The old PIO/non-DMA path tried to read the 512 data bytes directly from the
 * FDC FIFO. QEMU and a lot of real controllers behave much better with the
 * normal PC/AT path: 8237 DMA channel 2 + FDC result bytes at the end.
 */

#define FDC_DOR   0x3F2
#define FDC_MSR   0x3F4
#define FDC_FIFO  0x3F5
#define FDC_CCR   0x3F7

#define FDC_CMD_SPECIFY         0x03
#define FDC_CMD_SENSE_INTERRUPT 0x08
#define FDC_CMD_RECALIBRATE     0x07
#define FDC_CMD_SEEK            0x0F
#define FDC_CMD_READ_DATA       0x46  /* READ DATA + MFM, DMA transfer */

#define FLOPPY_SECTORS_PER_TRACK 18
#define FLOPPY_HEADS             2
#define FLOPPY_SECTOR_COUNT      2880

/* 0x9000 is below 64 KiB, below 16 MiB, and does not cross a DMA page. */
#define FLOPPY_DMA_BUFFER_PHYS   0x00009000U
#define FLOPPY_DMA_BUFFER        ((uint8_t *)FLOPPY_DMA_BUFFER_PHYS)

/* 8237 DMA controller ports for channel 2. */
#define DMA_MASK_REG             0x0A
#define DMA_MODE_REG             0x0B
#define DMA_CLEAR_FF             0x0C
#define DMA_CH2_ADDR             0x04
#define DMA_CH2_COUNT            0x05
#define DMA_CH2_PAGE             0x81

static uint8_t g_floppy_media = 0;

static void tiny_delay(uint32_t loops) {
    for (uint32_t i = 0; i < loops; i++) {
        if ((i & 0x3FFFU) == 0) bootsplash_pulse();
        io_wait();
    }
}

static bool fdc_wait_write(void) {
    for (uint32_t i = 0; i < 1000000; i++) {
        if ((i & 0x7FFFU) == 0) bootsplash_pulse();
        uint8_t msr = inb(FDC_MSR);
        if ((msr & 0xC0) == 0x80) return true; /* RQM=1, DIO=0 */
    }
    return false;
}

static bool fdc_wait_read(void) {
    for (uint32_t i = 0; i < 5000000; i++) {
        if ((i & 0x7FFFU) == 0) bootsplash_pulse();
        uint8_t msr = inb(FDC_MSR);
        if ((msr & 0xC0) == 0xC0) return true; /* RQM=1, DIO=1 */
    }
    return false;
}

static bool fdc_write(uint8_t value) {
    if (!fdc_wait_write()) return false;
    outb(FDC_FIFO, value);
    return true;
}

static bool fdc_read(uint8_t *value) {
    if (!value || !fdc_wait_read()) return false;
    *value = inb(FDC_FIFO);
    return true;
}

static bool fdc_sense(uint8_t *st0_out, uint8_t *cyl_out) {
    uint8_t st0 = 0;
    uint8_t cyl = 0;
    if (!fdc_write(FDC_CMD_SENSE_INTERRUPT)) return false;
    if (!fdc_read(&st0)) return false;
    if (!fdc_read(&cyl)) return false;
    if (st0_out) *st0_out = st0;
    if (cyl_out) *cyl_out = cyl;
    return true;
}

static void fdc_specify_dma(void) {
    /*
     * SRT/HUT = 0xDF, HLT/NDMA = 0x02.
     * Bit 0 of the second byte is NDMA. 0 = use DMA, 1 = non-DMA/PIO.
     */
    (void)fdc_write(FDC_CMD_SPECIFY);
    (void)fdc_write(0xDF);
    (void)fdc_write(0x02);
}

static void fdc_reset(void) {
    outb(FDC_DOR, 0x00);
    tiny_delay(50000);
    outb(FDC_DOR, 0x0C); /* controller enabled, IRQ/DMA enabled, motor off */
    tiny_delay(50000);
    outb(FDC_CCR, 0x00); /* 500 kbit/s for 1.44MB */

    /* A reset can leave pending interrupt states. Drain a few. */
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t st0, cyl;
        if (!fdc_sense(&st0, &cyl)) break;
    }

    fdc_specify_dma();
}

static void floppy_lba_to_chs(uint32_t lba, uint8_t *cyl, uint8_t *head, uint8_t *sector) {
    *cyl = (uint8_t)(lba / (FLOPPY_HEADS * FLOPPY_SECTORS_PER_TRACK));
    *head = (uint8_t)((lba % (FLOPPY_HEADS * FLOPPY_SECTORS_PER_TRACK)) / FLOPPY_SECTORS_PER_TRACK);
    *sector = (uint8_t)((lba % FLOPPY_SECTORS_PER_TRACK) + 1);
}

static void floppy_motor_on(void) {
    outb(FDC_DOR, 0x1C); /* motor A on, controller enabled, IRQ/DMA enabled */
    /* Real drives need spin-up time. QEMU ignores most of this. */
    tiny_delay(500000);
}

static void floppy_motor_off(void) {
    outb(FDC_DOR, 0x0C);
}

static bool floppy_recalibrate(void) {
    uint8_t st0 = 0;
    uint8_t cyl = 0xFF;

    floppy_motor_on();
    if (!fdc_write(FDC_CMD_RECALIBRATE)) return false;
    if (!fdc_write(0)) return false;

    for (uint32_t wait = 0; wait < 200000; wait++) {
        if ((wait & 0x3FFFU) == 0) bootsplash_pulse();
        io_wait();
    }
    if (!fdc_sense(&st0, &cyl)) return false;
    return ((st0 & 0xC0) == 0x00) && cyl == 0;
}

static bool floppy_seek(uint8_t cyl, uint8_t head) {
    uint8_t st0 = 0;
    uint8_t current_cyl = 0xFF;

    if (!fdc_write(FDC_CMD_SEEK)) return false;
    if (!fdc_write((uint8_t)((head << 2) | 0))) return false;
    if (!fdc_write(cyl)) return false;

    for (uint32_t wait = 0; wait < 200000; wait++) {
        if ((wait & 0x3FFFU) == 0) bootsplash_pulse();
        io_wait();
    }
    if (!fdc_sense(&st0, &current_cyl)) return false;
    return ((st0 & 0xC0) == 0x00) && current_cyl == cyl;
}

static void dma_floppy_setup_read(uint32_t phys_addr, uint16_t size) {
    uint16_t count = (uint16_t)(size - 1);

    /* Mask channel 2 while programming it. */
    outb(DMA_MASK_REG, 0x06);

    /* Address. */
    outb(DMA_CLEAR_FF, 0xFF);
    outb(DMA_CH2_ADDR, (uint8_t)(phys_addr & 0xFF));
    outb(DMA_CH2_ADDR, (uint8_t)((phys_addr >> 8) & 0xFF));
    outb(DMA_CH2_PAGE, (uint8_t)((phys_addr >> 16) & 0xFF));

    /* Count. */
    outb(DMA_CLEAR_FF, 0xFF);
    outb(DMA_CH2_COUNT, (uint8_t)(count & 0xFF));
    outb(DMA_CH2_COUNT, (uint8_t)((count >> 8) & 0xFF));

    /* Single transfer, address increment, no auto-init, write to memory, ch2. */
    outb(DMA_MODE_REG, 0x46);

    /* Unmask channel 2. */
    outb(DMA_MASK_REG, 0x02);
}

static bool fdc_read_result(uint8_t result[7]) {
    for (uint8_t i = 0; i < 7; i++) {
        if (!fdc_read(&result[i])) return false;
    }
    return true;
}

static void fdc_print_fail(uint32_t lba, uint8_t cyl, uint8_t head, uint8_t sector,
                           uint8_t stage, const uint8_t result[7]) {
    kprintf("[FDC] read fail lba=%u chs=%u/%u/%u stage=%u st=0x%x 0x%x 0x%x\n",
            lba, cyl, head, sector, stage,
            result ? result[0] : 0,
            result ? result[1] : 0,
            result ? result[2] : 0);
}

static bool floppy_read_one(uint32_t lba, void *buffer) {
    uint8_t cyl;
    uint8_t head;
    uint8_t sector;
    uint8_t result[7] = {0};
    uint8_t stage = 0;

    if (!buffer || lba >= FLOPPY_SECTOR_COUNT) return false;

    floppy_lba_to_chs(lba, &cyl, &head, &sector);
    floppy_motor_on();

    stage = 1;
    if (!floppy_seek(cyl, head)) goto fail;

    stage = 2;
    dma_floppy_setup_read(FLOPPY_DMA_BUFFER_PHYS, BLOCK_SECTOR_SIZE);

    stage = 3;
    if (!fdc_write(FDC_CMD_READ_DATA)) goto fail;
    if (!fdc_write((uint8_t)((head << 2) | 0))) goto fail;
    if (!fdc_write(cyl)) goto fail;
    if (!fdc_write(head)) goto fail;
    if (!fdc_write(sector)) goto fail;
    if (!fdc_write(2)) goto fail;                         /* 512 bytes */
    if (!fdc_write(FLOPPY_SECTORS_PER_TRACK)) goto fail;  /* EOT */
    if (!fdc_write(0x1B)) goto fail;                      /* GPL */
    if (!fdc_write(0xFF)) goto fail;                      /* DTL */

    stage = 4;
    if (!fdc_read_result(result)) goto fail;

    stage = 5;
    if ((result[0] & 0xC0) != 0 || result[1] != 0 || result[2] != 0) goto fail;

    kmemcpy(buffer, FLOPPY_DMA_BUFFER, BLOCK_SECTOR_SIZE);
    return true;

fail:
    fdc_print_fail(lba, cyl, head, sector, stage, result);
    return false;
}

static bool floppy_read_block(block_device_t *dev UNUSED, uint32_t lba, uint8_t count, void *buffer) {
    uint8_t *dst = (uint8_t *)buffer;
    if (!buffer || count == 0 || lba + count > FLOPPY_SECTOR_COUNT) return false;

    floppy_motor_on();
    for (uint8_t i = 0; i < count; i++) {
        bool ok = false;
        for (uint8_t retry = 0; retry < 5 && !ok; retry++) {
            ok = floppy_read_one(lba + i, dst + ((uint32_t)i * BLOCK_SECTOR_SIZE));
            if (!ok) {
                fdc_reset();
                (void)floppy_recalibrate();
            }
        }
        if (!ok) {
            floppy_motor_off();
            return false;
        }

        /*
         * Un pulso por sector leído es suficiente.
         * No animar dentro de fdc_wait_* ni tiny_delay: eso hace que la barra
         * vaya rapidísimo y puede ralentizar muchísimo el floppy.
         */
        bootsplash_pulse();
    }
    floppy_motor_off();
    return true;
}

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    io_wait();
    return inb(0x71);
}

void floppy_init(void) {
    uint8_t drives = cmos_read(0x10);
    g_floppy_media = (uint8_t)(drives >> 4);
    if (g_floppy_media == 0) {
        kprintf("  [FDC] No se detecto unidad de disquete en CMOS\n");
        return;
    }

    fdc_reset();
    (void)floppy_recalibrate();
    floppy_motor_off();

    /* IRQ6 queda habilitada, aunque este driver espera por polling del result phase. */
    pic_unmask_irq(6);
    block_register_ex("fd0", BLOCK_DEVICE_FLOPPY, FLOPPY_SECTOR_COUNT, BLOCK_SECTOR_SIZE, true, NULL, floppy_read_block);
    kprintf("  [FDC] fd0: 1.44MB CHS 80/2/18 DMA\n");
}
