#include "include/types.h"
#include "include/shell.h"
#include "include/vga.h"
#include "include/keyboard.h"
#include "include/memory.h"
#include "include/pic.h"
#include "include/idt.h"
#include "include/pit.h"
#include "include/block.h"
#include "include/vfs.h"
#include "include/pci.h"
#include "include/gfx.h"
#include "include/mouse.h"

static char history[SHELL_HISTORY_LEN][SHELL_MAX_CMD];
static int history_count = 0;
static int history_idx = 0;

static void history_add(const char *cmd) {
    if (!cmd || !cmd[0]) return;
    if (history_count > 0 && kstrcmp(history[(history_count - 1) % SHELL_HISTORY_LEN], cmd) == 0) return;
    kstrncpy(history[history_count % SHELL_HISTORY_LEN], cmd, SHELL_MAX_CMD - 1);
    history_count++;
    history_idx = history_count;
}

static void erase_input(size_t count) {
    while (count--) {
        vga_putchar('\b');
    }
}

static void readline(char *buf, size_t maxlen) {
    size_t pos = 0;
    kmemset(buf, 0, maxlen);
    history_idx = history_count;

    while (true) {
        char c = kbd_getchar();
        if (c == '\n' || c == '\r') {
            vga_putchar('\n');
            if (pos < maxlen - 1) buf[pos] = '\0';
            break;
        }
        if (c == '\b') {
            if (pos > 0) {
                pos--;
                buf[pos] = '\0';
                vga_putchar('\b');
            }
            continue;
        }
        if (c == KEY_UP) {
            if (history_idx > 0) {
                history_idx--;
                erase_input(pos);
                pos = 0;
                const char *cmd = history[history_idx % SHELL_HISTORY_LEN];
                size_t len = kstrlen(cmd);
                if (len >= maxlen - 1) len = maxlen - 1;
                kmemset(buf, 0, maxlen);
                kstrncpy(buf, cmd, maxlen - 1);
                for (size_t i = 0; i < len; i++) {
                    vga_putchar(cmd[i]);
                }
                pos = len;
            }
            continue;
        }
        if (c == KEY_DOWN) {
            if (history_idx < history_count) {
                history_idx++;
                erase_input(pos);
                pos = 0;
                if (history_idx < history_count) {
                    const char *cmd = history[history_idx % SHELL_HISTORY_LEN];
                    size_t len = kstrlen(cmd);
                    if (len >= maxlen - 1) len = maxlen - 1;
                    kmemset(buf, 0, maxlen);
                    kstrncpy(buf, cmd, maxlen - 1);
                    for (size_t i = 0; i < len; i++) {
                        vga_putchar(cmd[i]);
                    }
                    pos = len;
                }
            }
            continue;
        }
        if (pos + 1 < maxlen) {
            buf[pos++] = c;
            vga_putchar(c);
        }
    }
}

static int parse_args(char *line, char **argv, int max_args) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max_args) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    argv[argc] = NULL;
    return argc;
}

static int cmd_help(int argc UNUSED, char **argv UNUSED) {
    kprintf("Comandos disponibles:\n");
    kprintf("  help clear echo mem reboot halt color cpuid history\n");
    kprintf("  version uptime ticks panic cpu disks fatinfo mount\n");
    kprintf("  pwd cd ls cat open read close mkdir pci video gfx vesa mouse\n");
    kprintf("  lsmem lsirq gdt idt heap tasks (proximamente)\n");
    return 0;
}

static int cmd_clear(int argc UNUSED, char **argv UNUSED) {
    vga_clear();
    return 0;
}

static int cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) vga_putchar(' ');
        vga_puts(argv[i]);
    }
    vga_putchar('\n');
    return 0;
}

static int cmd_mem(int argc UNUSED, char **argv UNUSED) {
    heap_info_t info;
    mm_get_info(&info);
    kprintf("Memoria heap: total=%u used=%u free=%u blocks=%u\n", info.total_bytes, info.used_bytes, info.free_bytes, info.total_blocks);
    return 0;
}

static int cmd_reboot(int argc UNUSED, char **argv UNUSED) {
    kprintf("Reiniciando...\n");
    outb(0x64, 0xFE);
    return 0;
}

static int cmd_halt(int argc UNUSED, char **argv UNUSED) {
    kprintf("Deteniendo CPU.\n");
    for (;;) __asm__ volatile ("cli; hlt");
    return 0;
}

static int cmd_color(int argc, char **argv) {
    if (argc < 3) {
        kprintf("Uso: color <fg> <bg>\n");
        return 1;
    }
    int fg = 0, bg = 0;
    fg = (int)argv[1][0] - '0';
    bg = (int)argv[2][0] - '0';
    vga_set_color((vga_color_t)fg, (vga_color_t)bg);
    return 0;
}

static int cmd_cpuid(int argc UNUSED, char **argv UNUSED) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "0"(0));
    kprintf("CPUID vendor: %x %x %x\n", ebx, edx, ecx);
    return 0;
}

static int cmd_history(int argc UNUSED, char **argv UNUSED) {
    for (int i = 0; i < history_count && i < SHELL_HISTORY_LEN; i++) {
        kprintf("%d: %s\n", i + 1, history[i]);
    }
    return 0;
}

static int cmd_version(int argc UNUSED, char **argv UNUSED) {
    kprintf("BleskernOS debug shell v0.2\n");
    kprintf("Kernel: 32-bit protected mode\n");
    return 0;
}

static int cmd_uptime(int argc UNUSED, char **argv UNUSED) {
    uint32_t ticks = pit_get_ticks();
    kprintf("Uptime ticks: %u\n", ticks);
    kprintf("Approx seconds: %u\n", pit_get_uptime_seconds());
    return 0;
}

static int cmd_ticks(int argc UNUSED, char **argv UNUSED) {
    kprintf("Ticks: %u\n", pit_get_ticks());
    return 0;
}

static int cmd_panic(int argc, char **argv) {
    if (argc > 1) {
        kprintf("Kernel panic requested: %s\n", argv[1]);
    } else {
        kprintf("Kernel panic requested.\n");
    }
    __asm__ volatile ("int $3");
    return 0;
}

static int cmd_cpu(int argc UNUSED, char **argv UNUSED) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "0"(1));
    kprintf("CPU: 32-bit protected mode\n");
    kprintf("CPUID leaf 1: eax=0x%x ebx=0x%x ecx=0x%x edx=0x%x\n", eax, ebx, ecx, edx);
    return 0;
}

static int cmd_fatinfo(int argc UNUSED, char **argv UNUSED) {
    fat_fs_t fs;
    if (!vfs_get_fs_info(&fs)) {
        kprintf("No hay volumen FAT montado.\n");
        return 1;
    }
    kprintf("FAT%u device=%s lba=%u label=%s\n", fs.type, fs.device ? fs.device->name : "?", fs.volume_lba, fs.volume_label);
    kprintf("  bytes/sector=%u sectors/cluster=%u\n", fs.bytes_per_sector, fs.sectors_per_cluster);
    kprintf("  clusters=%u root_cluster=%u\n", fs.total_clusters, fs.root_dir_cluster);
    return 0;
}

static int cmd_disks(int argc UNUSED, char **argv UNUSED) {
    uint32_t count = block_count();
    if (count == 0) {
        kprintf("No hay dispositivos de bloque detectados.\n");
        return 1;
    }
    for (uint32_t i = 0; i < count; i++) {
        block_device_t *dev = block_at(i);
        if (!dev) continue;
        kprintf("%s\tsectores=%u\ttipo=%u\n", dev->name, dev->sector_count, dev->type);
    }
    return 0;
}

static int cmd_fatmount(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Uso: fatmount <fd0|ata0|ata0p1..ata0p4>\n");
        return 1;
    }
    if (!vfs_mount(argv[1])) {
        kprintf("No se pudo montar el volumen FAT solicitado.\n");
        return 1;
    }
    kprintf("FAT montado: %s\n", argv[1]);
    return 0;
}

static int cmd_pwd(int argc UNUSED, char **argv UNUSED) {
    kprintf("%s\n", vfs_getcwd());
    return 0;
}

static int cmd_cd(int argc, char **argv) {
    const char *path = "/";
    if (argc > 1) path = argv[1];
    if (!vfs_chdir(path)) {
        kprintf("No se pudo entrar a: %s\n", path);
        return 1;
    }
    return 0;
}

static int cmd_ls(int argc, char **argv) {
    const char *path = ".";
    vfs_dir_entry_t entries[32];
    uint32_t count = 0;
    if (argc > 1) path = argv[1];
    if (!vfs_listdir(path, entries, 32, &count)) {
        kprintf("No se pudo leer el directorio: %s\n", path);
        return 1;
    }
    for (uint32_t i = 0; i < count; i++) {
        kprintf("%s\t%s\t%u\n", entries[i].type == VFS_NODE_DIR ? "<DIR>" : "     ", entries[i].name, entries[i].size);
    }
    return 0;
}

static int cmd_cat(int argc, char **argv) {
    void *buffer = NULL;
    uint32_t size = 0;

    if (argc < 2) {
        kprintf("Uso: cat <archivo>\n");
        return 1;
    }
    if (!vfs_read_all(argv[1], &buffer, &size)) {
        kprintf("No se pudo leer el archivo.\n");
        return 1;
    }
    vga_puts((const char *)buffer);
    vga_putchar('\n');
    kfree(buffer);
    return 0;
}

static int parse_uint(const char *s) {
    int value = 0;
    if (!s || !s[0]) return -1;
    while (*s) {
        if (*s < '0' || *s > '9') return -1;
        value = (value * 10) + (*s - '0');
        s++;
    }
    return value;
}

static int parse_number(const char *s) {
    int value = 0;
    int base = 10;
    if (!s || !s[0]) return -1;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
        if (!s[0]) return -1;
    }
    while (*s) {
        int digit;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'f') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') digit = *s - 'A' + 10;
        else return -1;
        if (digit >= base) return -1;
        value = (value * base) + digit;
        s++;
    }
    return value;
}

static int cmd_open(int argc, char **argv) {
    int fd;
    if (argc < 2) {
        kprintf("Uso: open <archivo>\n");
        return 1;
    }
    fd = vfs_open(argv[1], VFS_O_RDONLY);
    if (fd < 0) {
        kprintf("No se pudo abrir: %s\n", argv[1]);
        return 1;
    }
    kprintf("fd=%d\n", fd);
    return 0;
}

static int cmd_read(int argc, char **argv) {
    int fd;
    int max = 128;
    int got;
    char buffer[129];
    if (argc < 2) {
        kprintf("Uso: read <fd> [bytes]\n");
        return 1;
    }
    fd = parse_uint(argv[1]);
    if (argc > 2) max = parse_uint(argv[2]);
    if (fd < 0 || max <= 0) {
        kprintf("Argumentos invalidos.\n");
        return 1;
    }
    if (max > 128) max = 128;
    got = vfs_read(fd, buffer, (uint32_t)max);
    if (got < 0) {
        kprintf("No se pudo leer fd=%d\n", fd);
        return 1;
    }
    buffer[got] = '\0';
    vga_puts(buffer);
    vga_putchar('\n');
    return 0;
}

static int cmd_close(int argc, char **argv) {
    int fd;
    if (argc < 2) {
        kprintf("Uso: close <fd>\n");
        return 1;
    }
    fd = parse_uint(argv[1]);
    if (!vfs_close(fd)) {
        kprintf("No se pudo cerrar fd=%d\n", fd);
        return 1;
    }
    return 0;
}

static int cmd_mkdir(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Uso: mkdir <ruta>\n");
        return 1;
    }
    if (!vfs_mkdir(argv[1])) {
        kprintf("mkdir aun no esta soportado para FAT en modo lectura.\n");
        return 1;
    }
    return 0;
}

static void pci_print_device(uint32_t index, const pci_device_t *dev) {
    kprintf("%u: %u:%u.%u ven=0x%x dev=0x%x class=0x%x/0x%x if=0x%x %s\n",
            index,
            dev->bus,
            dev->slot,
            dev->function,
            dev->vendor_id,
            dev->device_id,
            dev->class_code,
            dev->subclass,
            dev->prog_if,
            pci_class_name(dev->class_code, dev->subclass));
}

static void pci_print_usage(void) {
    kprintf("Uso: pci [list|info|bar|raw|read|find|enable|rescan]\n");
    kprintf("  pci info <index>\n");
    kprintf("  pci bar <index>\n");
    kprintf("  pci raw <index>\n");
    kprintf("  pci read <bus> <slot> <func> <offset>\n");
    kprintf("  pci find <class> [subclass]\n");
    kprintf("  pci enable <index> <io|mem|busmaster>\n");
}

static int cmd_pci(int argc, char **argv) {
    if (argc == 1 || kstrcmp(argv[1], "list") == 0) {
        uint32_t count = pci_device_count();
        if (count == 0) {
            kprintf("No hay dispositivos PCI detectados.\n");
            return 1;
        }
        for (uint32_t i = 0; i < count; i++) {
            const pci_device_t *dev = pci_device_at(i);
            if (dev) pci_print_device(i, dev);
        }
        return 0;
    }

    if (kstrcmp(argv[1], "info") == 0) {
        int index;
        const pci_device_t *dev;
        if (argc < 3) {
            kprintf("Uso: pci info <index>\n");
            return 1;
        }
        index = parse_number(argv[2]);
        dev = pci_device_at((uint32_t)index);
        if (index < 0 || !dev) {
            kprintf("Dispositivo PCI invalido.\n");
            return 1;
        }
        pci_print_device((uint32_t)index, dev);
        kprintf("  command=0x%x status=0x%x rev=0x%x prog_if=0x%x header=0x%x\n",
                dev->command, dev->status, dev->revision_id, dev->prog_if, dev->header_type);
        kprintf("  irq_line=%u irq_pin=%u\n", dev->interrupt_line, dev->interrupt_pin);
        if ((dev->header_type & 0x7F) == 0x01) {
            uint32_t buses = pci_config_read32(dev->bus, dev->slot, dev->function, 0x18);
            kprintf("  bridge buses: primary=%u secondary=%u subordinate=%u\n",
                    buses & 0xFF, (buses >> 8) & 0xFF, (buses >> 16) & 0xFF);
        }
        return 0;
    }

    if (kstrcmp(argv[1], "bar") == 0) {
        int index;
        const pci_device_t *dev;
        if (argc < 3) {
            kprintf("Uso: pci bar <index>\n");
            return 1;
        }
        index = parse_number(argv[2]);
        dev = pci_device_at((uint32_t)index);
        if (index < 0 || !dev) {
            kprintf("Dispositivo PCI invalido.\n");
            return 1;
        }
        for (uint32_t i = 0; i < PCI_BAR_COUNT; i++) {
            pci_bar_info_t info;
            if (!pci_get_bar_info(dev, (uint8_t)i, &info)) continue;
            if (info.is_io) {
                kprintf("  BAR%u io=0x%x size=%u raw=0x%x\n", i, info.base, info.size, info.raw);
            } else {
                kprintf("  BAR%u mem=0x%x size=%u %s%s raw=0x%x\n",
                        i,
                        info.base,
                        info.size,
                        info.is_64 ? "64bit " : "",
                        info.prefetchable ? "prefetch" : "",
                        info.raw);
            }
        }
        return 0;
    }

    if (kstrcmp(argv[1], "raw") == 0) {
        int index;
        const pci_device_t *dev;
        if (argc < 3) {
            kprintf("Uso: pci raw <index>\n");
            return 1;
        }
        index = parse_number(argv[2]);
        dev = pci_device_at((uint32_t)index);
        if (index < 0 || !dev) {
            kprintf("Dispositivo PCI invalido.\n");
            return 1;
        }
        for (uint8_t off = 0; off < 0x40; off += 16) {
            kprintf("  %x: %x %x %x %x\n",
                    off,
                    pci_config_read32(dev->bus, dev->slot, dev->function, off),
                    pci_config_read32(dev->bus, dev->slot, dev->function, (uint8_t)(off + 4)),
                    pci_config_read32(dev->bus, dev->slot, dev->function, (uint8_t)(off + 8)),
                    pci_config_read32(dev->bus, dev->slot, dev->function, (uint8_t)(off + 12)));
        }
        return 0;
    }

    if (kstrcmp(argv[1], "read") == 0) {
        int bus, slot, func, offset;
        if (argc < 6) {
            kprintf("Uso: pci read <bus> <slot> <func> <offset>\n");
            return 1;
        }
        bus = parse_number(argv[2]);
        slot = parse_number(argv[3]);
        func = parse_number(argv[4]);
        offset = parse_number(argv[5]);
        if (bus < 0 || bus > 255 || slot < 0 || slot > 31 || func < 0 || func > 7 || offset < 0 || offset > 255) {
            kprintf("Argumentos PCI invalidos.\n");
            return 1;
        }
        kprintf("PCI %u:%u.%u[0x%x] = 0x%x\n",
                bus, slot, func, offset & 0xFC,
                pci_config_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, (uint8_t)offset));
        return 0;
    }

    if (kstrcmp(argv[1], "find") == 0) {
        int class_code;
        int subclass = -1;
        int found;
        uint32_t start = 0;
        if (argc < 3) {
            kprintf("Uso: pci find <class> [subclass]\n");
            return 1;
        }
        class_code = parse_number(argv[2]);
        if (argc > 3) subclass = parse_number(argv[3]);
        if (class_code < 0 || class_code > 255 || subclass > 255) {
            kprintf("Argumentos PCI invalidos.\n");
            return 1;
        }
        while ((found = pci_find_by_class((uint8_t)class_code, subclass, start)) >= 0) {
            const pci_device_t *dev = pci_device_at((uint32_t)found);
            if (dev) pci_print_device((uint32_t)found, dev);
            start = (uint32_t)found + 1;
        }
        return 0;
    }

    if (kstrcmp(argv[1], "enable") == 0) {
        int index;
        uint16_t bit = 0;
        const pci_device_t *dev;
        if (argc < 4) {
            kprintf("Uso: pci enable <index> <io|mem|busmaster>\n");
            return 1;
        }
        index = parse_number(argv[2]);
        dev = pci_device_at((uint32_t)index);
        if (index < 0 || !dev) {
            kprintf("Dispositivo PCI invalido.\n");
            return 1;
        }
        if (kstrcmp(argv[3], "io") == 0) bit = PCI_COMMAND_IO;
        else if (kstrcmp(argv[3], "mem") == 0) bit = PCI_COMMAND_MEMORY;
        else if (kstrcmp(argv[3], "busmaster") == 0) bit = PCI_COMMAND_BUSMASTER;
        else {
            kprintf("Bit desconocido: %s\n", argv[3]);
            return 1;
        }
        if (!pci_enable_command(dev, bit)) {
            kprintf("No se pudo actualizar command.\n");
            return 1;
        }
        pci_refresh_device((uint32_t)index);
        kprintf("command=0x%x\n", pci_device_at((uint32_t)index)->command);
        return 0;
    }

    if (kstrcmp(argv[1], "rescan") == 0) {
        pci_init();
        return 0;
    }

    pci_print_usage();
    return 1;
}

static int cmd_video(int argc, char **argv) {
    const gfx_info_t *info = gfx_get_info();
    video_type_t type = gfx_detect_video_type();
    const char *mode = "unknown";

    if (argc > 1 && kstrcmp(argv[1], "text") == 0) {
        gfx_set_text_mode();
        vga_init();
        kprintf("Modo texto restaurado.\n");
        return 0;
    }

    if (info->mode == GFX_MODE_TEXT) mode = "text";
    else if (info->mode == GFX_MODE_VGA_13H) mode = "vga13h";
    else if (info->mode == GFX_MODE_VGA_12H) mode = "vga12h";
    else if (info->mode == GFX_MODE_VESA_LFB) mode = "vesa-lfb";

    kprintf("Video BDA: %s (0x%x)\n", gfx_video_type_name(type), type);
    kprintf("Modo: %s fb=0x%x %ux%u pitch=%u bpp=%u\n",
            mode,
            info->framebuffer,
            info->width,
            info->height,
            info->pitch,
            info->bpp);
    kprintf("Uso: video [info|text]\n");
    return 0;
}

static int cmd_gfx(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Uso: gfx <mode13|demo|clear|pixel|rect|line|text|palette>\n");
        return 1;
    }

    if (kstrcmp(argv[1], "mode13") == 0) {
        gfx_set_mode13h();
        return 0;
    }
    if (kstrcmp(argv[1], "mode12") == 0) {
        gfx_set_mode12h();
        return 0;
    }
    if (kstrcmp(argv[1], "demo") == 0) {
        gfx_set_mode13h();
        gfx_demo();
        return 0;
    }
    if (kstrcmp(argv[1], "clear") == 0) {
        int color = argc > 2 ? parse_number(argv[2]) : 0;
        if (color < 0 || color > 255) {
            kprintf("Color invalido.\n");
            return 1;
        }
        gfx_clear((uint8_t)color);
        return 0;
    }
    if (kstrcmp(argv[1], "pixel") == 0) {
        int x, y, color;
        if (argc < 5) {
            kprintf("Uso: gfx pixel <x> <y> <color>\n");
            return 1;
        }
        x = parse_number(argv[2]);
        y = parse_number(argv[3]);
        color = parse_number(argv[4]);
        if (x < 0 || y < 0 || color < 0 || color > 255) return 1;
        gfx_putpixel(x, y, (uint8_t)color);
        return 0;
    }
    if (kstrcmp(argv[1], "rect") == 0) {
        int x, y, w, h, color;
        if (argc < 7) {
            kprintf("Uso: gfx rect <x> <y> <w> <h> <color>\n");
            return 1;
        }
        x = parse_number(argv[2]);
        y = parse_number(argv[3]);
        w = parse_number(argv[4]);
        h = parse_number(argv[5]);
        color = parse_number(argv[6]);
        if (color < 0 || color > 255) return 1;
        gfx_fill_rect(x, y, w, h, (uint8_t)color);
        return 0;
    }
    if (kstrcmp(argv[1], "line") == 0) {
        int x0, y0, x1, y1, color;
        if (argc < 7) {
            kprintf("Uso: gfx line <x0> <y0> <x1> <y1> <color>\n");
            return 1;
        }
        x0 = parse_number(argv[2]);
        y0 = parse_number(argv[3]);
        x1 = parse_number(argv[4]);
        y1 = parse_number(argv[5]);
        color = parse_number(argv[6]);
        if (color < 0 || color > 255) return 1;
        gfx_draw_line(x0, y0, x1, y1, (uint8_t)color);
        return 0;
    }
    if (kstrcmp(argv[1], "text") == 0) {
        int x, y, fg;
        if (argc < 6) {
            kprintf("Uso: gfx text <x> <y> <color> <texto>\n");
            return 1;
        }
        x = parse_number(argv[2]);
        y = parse_number(argv[3]);
        fg = parse_number(argv[4]);
        if (fg < 0 || fg > 255) return 1;
        gfx_draw_string(x, y, argv[5], (uint8_t)fg, 0, false);
        return 0;
    }
    if (kstrcmp(argv[1], "palette") == 0) {
        int index, r, g, b;
        if (argc < 6) {
            kprintf("Uso: gfx palette <index> <r> <g> <b>\n");
            return 1;
        }
        index = parse_number(argv[2]);
        r = parse_number(argv[3]);
        g = parse_number(argv[4]);
        b = parse_number(argv[5]);
        if (index < 0 || index > 255 || r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) return 1;
        gfx_set_palette_color((uint8_t)index, (uint8_t)r, (uint8_t)g, (uint8_t)b);
        return 0;
    }

    kprintf("Uso: gfx <mode12|mode13|demo|clear|pixel|rect|line|text|palette>\n");
    return 1;
}

static int cmd_vesa(int argc, char **argv) {
    const gfx_info_t *info = gfx_get_info();
    if (argc == 1 || kstrcmp(argv[1], "info") == 0) {
        kprintf("VESA LFB: %s\n", gfx_has_vesa_lfb() ? "attached" : "not attached");
        kprintf("fb=0x%x %ux%u pitch=%u bpp=%u mode=%u\n",
                info->framebuffer, info->width, info->height, info->pitch, info->bpp, info->mode);
        return 0;
    }
    if (kstrcmp(argv[1], "attach") == 0) {
        int fb, w, h, pitch, bpp;
        if (argc < 7) {
            kprintf("Uso: vesa attach <fb> <w> <h> <pitch> <bpp>\n");
            return 1;
        }
        fb = parse_number(argv[2]);
        w = parse_number(argv[3]);
        h = parse_number(argv[4]);
        pitch = parse_number(argv[5]);
        bpp = parse_number(argv[6]);
        if (fb <= 0 || w <= 0 || h <= 0 || pitch <= 0 || bpp <= 0) {
            kprintf("Argumentos VESA invalidos.\n");
            return 1;
        }
        if (!gfx_attach_vesa_lfb((uint32_t)fb, (uint16_t)w, (uint16_t)h, (uint16_t)pitch, (uint8_t)bpp)) {
            kprintf("No se pudo adjuntar LFB VESA.\n");
            return 1;
        }
        kprintf("VESA LFB adjuntado.\n");
        return 0;
    }
    if (kstrcmp(argv[1], "clear") == 0) {
        int rgb = argc > 2 ? parse_number(argv[2]) : 0;
        if (rgb < 0) return 1;
        gfx_clear_rgb((uint32_t)rgb);
        return 0;
    }
    if (kstrcmp(argv[1], "pixel") == 0) {
        int x, y, rgb;
        if (argc < 5) {
            kprintf("Uso: vesa pixel <x> <y> <rgb>\n");
            return 1;
        }
        x = parse_number(argv[2]);
        y = parse_number(argv[3]);
        rgb = parse_number(argv[4]);
        if (x < 0 || y < 0 || rgb < 0) return 1;
        gfx_putpixel_rgb(x, y, (uint32_t)rgb);
        return 0;
    }
    if (kstrcmp(argv[1], "rect") == 0) {
        int x, y, w, h, rgb;
        if (argc < 7) {
            kprintf("Uso: vesa rect <x> <y> <w> <h> <rgb>\n");
            return 1;
        }
        x = parse_number(argv[2]);
        y = parse_number(argv[3]);
        w = parse_number(argv[4]);
        h = parse_number(argv[5]);
        rgb = parse_number(argv[6]);
        if (rgb < 0) return 1;
        gfx_fill_rect_rgb(x, y, w, h, (uint32_t)rgb);
        return 0;
    }
    kprintf("Uso: vesa [info|attach|clear|pixel|rect]\n");
    return 1;
}

static int cmd_mouse(int argc, char **argv) {
    mouse_state_t state;
    const gfx_info_t *info = gfx_get_info();

    if (argc > 1 && kstrcmp(argv[1], "center") == 0) {
        mouse_set_position(info->width ? info->width / 2 : 0, info->height ? info->height / 2 : 0);
    } else if (argc > 1 && kstrcmp(argv[1], "bounds") == 0) {
        int w;
        int h;
        if (argc < 4) {
            kprintf("Uso: mouse bounds <w> <h>\n");
            return 1;
        }
        w = parse_number(argv[2]);
        h = parse_number(argv[3]);
        if (w <= 0 || h <= 0) {
            kprintf("Limites invalidos.\n");
            return 1;
        }
        mouse_set_bounds(w, h);
    } else if (argc > 1) {
        kprintf("Uso: mouse [center|bounds]\n");
        return 1;
    }

    mouse_get_state(&state);
    kprintf("Mouse: %s id=%u packet=%u packets=%u\n",
            state.present ? "present" : "not-present",
            state.device_id,
            state.packet_size,
            state.packets);
    kprintf("  irq=%u bytes=%u init=%u err=%u\n",
            state.irq_count,
            state.byte_count,
            state.init_step,
            state.last_error);
    kprintf("  pos=%d,%d delta=%d,%d wheel=%d buttons=0x%x\n",
            state.x,
            state.y,
            state.dx,
            state.dy,
            state.wheel,
            state.buttons);
    return state.present ? 0 : 1;
}

static int cmd_stub(int argc UNUSED, char **argv UNUSED) {
    kprintf("Comando pendiente de implementar.\n");
    return 0;
}

static const shell_cmd_t commands[] = {
    {"help", "Muestra este mensaje de ayuda", cmd_help},
    {"clear", "Limpia la pantalla", cmd_clear},
    {"echo", "Imprime texto", cmd_echo},
    {"mem", "Muestra estado de la memoria", cmd_mem},
    {"reboot", "Reinicia el sistema", cmd_reboot},
    {"halt", "Detiene el sistema", cmd_halt},
    {"color", "Cambia color", cmd_color},
    {"cpuid", "Muestra informacion del CPU", cmd_cpuid},
    {"history", "Muestra el historial", cmd_history},
    {"version", "Muestra la version del kernel", cmd_version},
    {"uptime", "Muestra tiempo de actividad aproximado", cmd_uptime},
    {"ticks", "Muestra el contador de ticks", cmd_ticks},
    {"panic", "Dispara una excepcion de depuracion", cmd_panic},
    {"cpu", "Muestra informacion basica del CPU", cmd_cpu},
    {"disks", "Lista discos y disquetes detectados", cmd_disks},
    {"fatinfo", "Muestra informacion del volumen FAT activo", cmd_fatinfo},
    {"mount", "Monta un volumen desde fd0, ata0 o ata0pN", cmd_fatmount},
    {"fatmount", "Monta FAT desde fd0, ata0 o ata0pN", cmd_fatmount},
    {"pwd", "Muestra el directorio actual", cmd_pwd},
    {"cd", "Cambia el directorio actual", cmd_cd},
    {"ls", "Lista un directorio", cmd_ls},
    {"cat", "Muestra un archivo", cmd_cat},
    {"open", "Abre un archivo y devuelve fd", cmd_open},
    {"read", "Lee desde un fd", cmd_read},
    {"close", "Cierra un fd", cmd_close},
    {"mkdir", "Crea un directorio", cmd_mkdir},
    {"pci", "Lista e inspecciona dispositivos PCI", cmd_pci},
    {"video", "Muestra info de video o vuelve a texto", cmd_video},
    {"gfx", "Prueba primitivas graficas VGA", cmd_gfx},
    {"vesa", "Prueba backend VESA LFB", cmd_vesa},
    {"mouse", "Muestra estado del mouse PS/2", cmd_mouse},
    {"lsmem", "Pendiente", cmd_stub},
    {"lsirq", "Pendiente", cmd_stub},
    {"gdt", "Pendiente", cmd_stub},
    {"idt", "Pendiente", cmd_stub},
    {"heap", "Pendiente", cmd_stub},
    {"tasks", "Pendiente", cmd_stub},
    {NULL, NULL, NULL}
};

static void execute(char *line) {
    char *argv[SHELL_MAX_ARGS + 1];
    int argc = parse_args(line, argv, SHELL_MAX_ARGS);
    if (argc == 0) return;

    history_add(line);

    for (int i = 0; commands[i].name; i++) {
        if (kstrcmp(argv[0], commands[i].name) == 0) {
            commands[i].func(argc, argv);
            return;
        }
    }

    kprintf("Comando desconocido: %s\n", argv[0]);
}

static void print_banner(void) {
    vga_clear();
    kprintf("=================================================================\n");
    kprintf("  BleskernOS Shell v0.1\n");
    kprintf("  Escribe 'help' para ver los comandos disponibles.\n");
    kprintf("=================================================================\n");
}

void shell_run(void) {
    char line[SHELL_MAX_CMD];
    print_banner();
    while (true) {
        kprintf("%s> ", vfs_getcwd());
        readline(line, sizeof(line));
        execute(line);
    }
}
