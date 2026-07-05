#include "../include/floppy.h"
#include "../include/block.h"
#include "../include/memory.h"
#include "../include/pic.h"
#include "../include/vga.h"

#define FDC_DOR   0x3F2
#define FDC_MSR   0x3F4
#define FDC_FIFO  0x3F5
#define FDC_CCR   0x3F7

#define FDC_CMD_SPECIFY         0x03
#define FDC_CMD_SENSE_INTERRUPT 0x08
#define FDC_CMD_RECALIBRATE     0x07
#define FDC_CMD_SEEK            0x0F
#define FDC_CMD_READ_DATA       0x66

#define FLOPPY_SECTORS_PER_TRACK 18
#define FLOPPY_HEADS             2
#define FLOPPY_SECTOR_COUNT      2880

static uint8_t g_floppy_media = 0;

static bool fdc_wait_write(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        uint8_t msr = inb(FDC_MSR);
        if ((msr & 0xC0) == 0x80) return true;
    }
    return false;
}

static bool fdc_wait_read(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        uint8_t msr = inb(FDC_MSR);
        if ((msr & 0xC0) == 0xC0) return true;
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

static void floppy_lba_to_chs(uint32_t lba, uint8_t *cyl, uint8_t *head, uint8_t *sector) {
    *cyl = (uint8_t)(lba / (FLOPPY_HEADS * FLOPPY_SECTORS_PER_TRACK));
    *head = (uint8_t)((lba % (FLOPPY_HEADS * FLOPPY_SECTORS_PER_TRACK)) / FLOPPY_SECTORS_PER_TRACK);
    *sector = (uint8_t)((lba % FLOPPY_SECTORS_PER_TRACK) + 1);
}

static void floppy_motor_on(void) {
    outb(FDC_DOR, 0x1C);
    for (uint32_t i = 0; i < 10000; i++) io_wait();
}

static void floppy_motor_off(void) {
    outb(FDC_DOR, 0x0C);
}

static void fdc_sense(void) {
    uint8_t st0;
    uint8_t cyl;
    if (!fdc_write(FDC_CMD_SENSE_INTERRUPT)) return;
    (void)fdc_read(&st0);
    (void)fdc_read(&cyl);
}

static bool floppy_recalibrate(void) {
    floppy_motor_on();
    if (!fdc_write(FDC_CMD_RECALIBRATE)) return false;
    if (!fdc_write(0)) return false;
    for (uint32_t i = 0; i < 10000; i++) io_wait();
    fdc_sense();
    return true;
}

static bool floppy_seek(uint8_t cyl, uint8_t head) {
    if (!fdc_write(FDC_CMD_SEEK)) return false;
    if (!fdc_write((uint8_t)((head << 2) | 0))) return false;
    if (!fdc_write(cyl)) return false;
    for (uint32_t i = 0; i < 10000; i++) io_wait();
    fdc_sense();
    return true;
}

static bool floppy_read_one(uint32_t lba, void *buffer) {
    uint8_t cyl;
    uint8_t head;
    uint8_t sector;
    uint8_t *dst = (uint8_t *)buffer;
    uint8_t result[7];

    floppy_lba_to_chs(lba, &cyl, &head, &sector);
    floppy_motor_on();
    if (!floppy_seek(cyl, head)) return false;

    if (!fdc_write(FDC_CMD_READ_DATA)) return false;
    if (!fdc_write((uint8_t)((head << 2) | 0))) return false;
    if (!fdc_write(cyl)) return false;
    if (!fdc_write(head)) return false;
    if (!fdc_write(sector)) return false;
    if (!fdc_write(2)) return false;
    if (!fdc_write(sector)) return false;
    if (!fdc_write(0x1B)) return false;
    if (!fdc_write(0xFF)) return false;

    for (uint32_t i = 0; i < BLOCK_SECTOR_SIZE; i++) {
        if (!fdc_read(&dst[i])) return false;
    }

    for (uint32_t i = 0; i < 7; i++) {
        if (!fdc_read(&result[i])) return false;
    }

    return (result[0] & 0xC0) == 0 && result[1] == 0 && result[2] == 0;
}

static bool floppy_read_block(block_device_t *dev UNUSED, uint32_t lba, uint8_t count, void *buffer) {
    uint8_t *dst = (uint8_t *)buffer;
    if (!buffer || count == 0 || lba + count > FLOPPY_SECTOR_COUNT) return false;

    for (uint8_t i = 0; i < count; i++) {
        bool ok = false;
        for (uint8_t retry = 0; retry < 3 && !ok; retry++) {
            ok = floppy_read_one(lba + i, dst + (i * BLOCK_SECTOR_SIZE));
            if (!ok) floppy_recalibrate();
        }
        if (!ok) {
            floppy_motor_off();
            return false;
        }
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

    outb(FDC_DOR, 0x00);
    for (uint32_t i = 0; i < 10000; i++) io_wait();
    outb(FDC_DOR, 0x0C);
    outb(FDC_CCR, 0x00);

    fdc_sense();
    fdc_write(FDC_CMD_SPECIFY);
    fdc_write(0xDF);
    fdc_write(0x03);
    floppy_recalibrate();
    floppy_motor_off();

    /* El FDC senala seek/read por IRQ6 incluso en el modo de transferencia PIO. */
    pic_unmask_irq(6);
    block_register("fd0", BLOCK_DEVICE_FLOPPY, FLOPPY_SECTOR_COUNT, NULL, floppy_read_block);
    kprintf("  [FDC] fd0: 1.44MB CHS 80/2/18\n");
}
