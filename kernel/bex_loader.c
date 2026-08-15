#include "include/bex_loader.h"
#include "include/elf_loader.h"
#include "include/memory.h"
#include "include/vfs.h"
#include "string.h"

/*
 * BEX is the stable executable identity shared by BOS and BlesKernOS.
 *
 * Historical BOS images begin with BEX1..BEX4.  Native BlesKernOS BEX files
 * are ELF32 ET_REL images with a .BEX filename.  Keeping the ELF header at
 * offset zero means today's loader and toolchain remain simple while the
 * external format no longer exposes the linker intermediate ".O" suffix.
 *
 * A future BOS/VM plugs into bex_execute_program() for the four legacy
 * formats; the native kernel does not need to acquire 16-bit assumptions.
 */

static const char *g_bex_error = "sin error";

static bool bex_prefix(const uint8_t *image, uint32_t size,
                       const char *magic, uint32_t magic_size) {
    return image && size >= magic_size &&
           kmemcmp(image, magic, magic_size) == 0;
}

bex_format_t bex_identify(const void *image_data, uint32_t size) {
    const uint8_t *image = (const uint8_t *)image_data;

    if (bex_prefix(image, size, "BEX1", 4U)) return BEX_FORMAT_BOS1;
    if (bex_prefix(image, size, "BEX2", 4U)) return BEX_FORMAT_BOS2;
    if (bex_prefix(image, size, "BEX3", 4U)) return BEX_FORMAT_BOS3;
    if (bex_prefix(image, size, "BEX4", 4U)) return BEX_FORMAT_BOS4;
    if (size >= 5U && image[0] == 0x7FU && image[1] == 'E' &&
        image[2] == 'L' && image[3] == 'F' && image[4] == 1U)
        return BEX_FORMAT_BLES32;
    return BEX_FORMAT_UNKNOWN;
}

const char *bex_format_name(bex_format_t format) {
    switch (format) {
        case BEX_FORMAT_BOS1: return "BEX1 (BOS 1.x)";
        case BEX_FORMAT_BOS2: return "BEX2 (BOS 2.x)";
        case BEX_FORMAT_BOS3: return "BEX3 (BOS 3.x)";
        case BEX_FORMAT_BOS4: return "BEX4 (BOS 4.x)";
        case BEX_FORMAT_BLES32: return "BEX32 (BlesKernOS)";
        default: return "BEX desconocido";
    }
}

bool bex_execute_program(const char *path, struct gui_desktop *desktop,
                         const char *launch_arg) {
    uint8_t header[5];
    int fd;
    int got;
    bex_format_t format;

    if (!path || !desktop) {
        g_bex_error = "argumentos BEX invalidos";
        return false;
    }
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        g_bex_error = "no se pudo leer el archivo BEX";
        return false;
    }
    got = vfs_read(fd, header, sizeof(header));
    vfs_close(fd);
    if (got < 4) {
        g_bex_error = "cabecera BEX truncada";
        return false;
    }
    format = bex_identify(header, (uint32_t)got);

    if (format == BEX_FORMAT_BLES32) {
        if (!elf_execute_program_ex(path, desktop, launch_arg)) {
            g_bex_error = elf_last_error();
            return false;
        }
        g_bex_error = "sin error";
        return true;
    }
    if (format >= BEX_FORMAT_BOS1 && format <= BEX_FORMAT_BOS4) {
        g_bex_error = "BOS/VM todavia no esta instalado para este BEX historico";
        return false;
    }
    g_bex_error = "cabecera BEX no reconocida";
    return false;
}

const char *bex_last_error(void) {
    return g_bex_error;
}
