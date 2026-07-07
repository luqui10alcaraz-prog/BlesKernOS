#include "include/elf_loader.h"
#include "include/memory.h"
#include "include/vfs.h"
#include "include/task.h"
#include "include/pit.h"
#include "include/keyboard.h"
#include "include/sound.h"
#include "include/vga.h"
#include "../gui/gui.h"
#include <TGL/gl.h>
#include "zbuffer.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "strings.h"
#include "ctype.h"
#include "math.h"
#include "errno.h"
#include "sys/stat.h"
#include "include/rtc.h"

#define EI_NIDENT 16
#define ET_REL 1
#define EM_386 3
#define EV_CURRENT 1
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_NOBITS 8
#define SHT_REL 9
#define SHF_ALLOC 0x2
#define SHN_UNDEF 0
#define SHN_ABS 0xFFF1
#define R_386_32 1
#define R_386_PC32 2

typedef struct {
    uint8_t ident[EI_NIDENT];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} PACKED elf32_header_t;

typedef struct {
    uint32_t name;
    uint32_t type;
    uint32_t flags;
    uint32_t addr;
    uint32_t offset;
    uint32_t size;
    uint32_t link;
    uint32_t info;
    uint32_t addralign;
    uint32_t entsize;
} PACKED elf32_section_t;

typedef struct {
    uint32_t name;
    uint32_t value;
    uint32_t size;
    uint8_t info;
    uint8_t other;
    uint16_t shndx;
} PACKED elf32_symbol_t;

typedef struct {
    uint32_t offset;
    uint32_t info;
} PACKED elf32_rel_t;

typedef void (*elf_program_entry_t)(gui_desktop_t *desktop);

static const char *g_elf_error = "sin error";
static void *g_loaded_image;
static elf_program_entry_t g_loaded_entry;
static char g_loaded_path[VFS_MAX_PATH];

extern int __divdi3(void);

static bool elf_range_ok(uint32_t offset, uint32_t length, uint32_t total) {
    return offset <= total && length <= total - offset;
}

static uint32_t elf_align(uint32_t value, uint32_t alignment) {
    if (alignment <= 1) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

#define EXPORT(symbol) \
    if (kstrcmp(name, #symbol) == 0) return (uint32_t)(uintptr_t)&symbol

static uint32_t elf_kernel_symbol(const char *name) {
    /* GCC / runtime */
    EXPORT(__divdi3);

    /* libc / stdio / stdlib / string */
    EXPORT(abs);
    EXPORT(atof);
    EXPORT(atoi);
    EXPORT(calloc);
    EXPORT(errno);
    EXPORT(exit);
    EXPORT(fabs);
    EXPORT(cos);
    EXPORT(sin);
    EXPORT(sqrt);
    EXPORT(floor);
    EXPORT(pow);
    EXPORT(fclose);
    EXPORT(fflush);
    EXPORT(fopen);
    EXPORT(fprintf);
    EXPORT(fread);
    EXPORT(free);
    EXPORT(fseek);
    EXPORT(ftell);
    EXPORT(fwrite);
    EXPORT(malloc);
    EXPORT(memcpy);
    EXPORT(memset);
    EXPORT(mkdir);
    EXPORT(printf);
    EXPORT(putchar);
    EXPORT(puts);
    EXPORT(realloc);
    EXPORT(remove);
    EXPORT(rename);
    EXPORT(snprintf);
    EXPORT(sscanf);
    EXPORT(stderr);
    EXPORT(stdout);
    EXPORT(strcasecmp);
    EXPORT(strchr);
    EXPORT(strcmp);
    EXPORT(strdup);
    EXPORT(strlen);
    EXPORT(strncasecmp);
    EXPORT(strncmp);
    EXPORT(strncpy);
    EXPORT(strrchr);
    EXPORT(strstr);
    EXPORT(system);
    EXPORT(toupper);
    EXPORT(vfprintf);
    EXPORT(vsnprintf);

    /* GUI / Desktop */
    EXPORT(gui_desktop_create_window);
    EXPORT(gui_desktop_focus_window);
    EXPORT(gui_desktop_raise_window);
    EXPORT(gui_desktop_remove_window);
    EXPORT(gui_desktop_register_program);
    EXPORT(gui_desktop_unregister_program);

    /* GUI / Drawing */
    EXPORT(gui_gfx_clear);
    EXPORT(gui_gfx_fill_rect);
    EXPORT(gui_gfx_draw_rect);
    EXPORT(gui_gfx_putpixel);
    EXPORT(gui_gfx_invalidate_front);
    EXPORT(gui_font_draw_string);
    EXPORT(gui_font_draw_string_clipped);
    EXPORT(gui_font_draw_string_scaled);
    EXPORT(gui_request_paint);
    EXPORT(gui_get_desktop);

    /* GUI / Window */
    EXPORT(gui_window_content_top);
    EXPORT(gui_window_destroy);
    EXPORT(gui_window_set_content);
    EXPORT(gui_window_set_event_handler);
    EXPORT(gui_window_set_min_size);
    EXPORT(gui_window_close);
    EXPORT(gui_window_minimize);
    EXPORT(gui_window_restore);

    /* GUI / Widgets */
    EXPORT(gui_widget_create);
    EXPORT(gui_widget_handle_event);
    EXPORT(gui_widget_paint);

    /* TinyGL / ZBuffer for external screen savers */
    EXPORT(ZB_open);
    EXPORT(ZB_close);
    EXPORT(ZB_copyFrameBuffer);

    EXPORT(glBegin);
    EXPORT(tglBegin);
    EXPORT(glClear);
    EXPORT(tglClear);
    EXPORT(glClearColor);
    EXPORT(tglClearColor);
    EXPORT(glClose);
    EXPORT(tglClose);
    EXPORT(glColor3f);
    EXPORT(tglColor3f);
    EXPORT(glColor3fv);
    EXPORT(tglColor3fv);
    EXPORT(glDepthMask);
    EXPORT(tglDepthMask);
    EXPORT(glDisable);
    EXPORT(tglDisable);
    EXPORT(glDrawText);
    EXPORT(tglDrawText);
    EXPORT(glEnable);
    EXPORT(tglEnable);
    EXPORT(glEnd);
    EXPORT(tglEnd);
    EXPORT(glFrustum);
    EXPORT(tglFrustum);
    EXPORT(glInit);
    EXPORT(tglInit);
    EXPORT(glLightModelfv);
    EXPORT(tglLightModelfv);
    EXPORT(glLightfv);
    EXPORT(tglLightfv);
    EXPORT(glLoadIdentity);
    EXPORT(tglLoadIdentity);
    EXPORT(glMaterialfv);
    EXPORT(tglMaterialfv);
    EXPORT(glMatrixMode);
    EXPORT(tglMatrixMode);
    EXPORT(glNormal3f);
    EXPORT(tglNormal3f);
    EXPORT(glRotatef);
    EXPORT(tglRotatef);
    EXPORT(glSetEnableSpecular);
    EXPORT(tglSetEnableSpecular);
    EXPORT(glShadeModel);
    EXPORT(tglShadeModel);
    EXPORT(glTextSize);
    EXPORT(tglTextSize);
    EXPORT(glTranslatef);
    EXPORT(tglTranslatef);
    EXPORT(glVertex3f);
    EXPORT(tglVertex3f);
    EXPORT(glViewport);
    EXPORT(tglViewport);

    /* Keyboard */
    EXPORT(kbd_next_event);

    /* Kernel memory/string helpers */
    EXPORT(kfree);
    EXPORT(kmemcpy);
    EXPORT(kmemset);
    EXPORT(kprintf);
    EXPORT(kstrncpy);
    EXPORT(kzalloc);

    /* Libc integration */
    EXPORT(libc_set_exit_handler);

    /* PIT / timing */
    EXPORT(pit_get_frequency_hz);
    EXPORT(pit_get_ticks);

    /* RTC */
    EXPORT(rtc_get_time);
    EXPORT(rtc_get_date);
    EXPORT(rtc_get_datetime);

    /* Sound */
    EXPORT(sound_pcm_available);
    EXPORT(sound_pcm_is_busy);
    EXPORT(sound_play_pcm_u8);
    EXPORT(sound_stop);

    /* Tasks */
    EXPORT(task_bind_window);
    EXPORT(task_create);
    EXPORT(task_current_pid);
    EXPORT(task_exit);
    EXPORT(task_exit_requested);
    EXPORT(task_set_memory_hint);
    EXPORT(task_sleep);

    /* VFS */
    EXPORT(vfs_close);
    EXPORT(vfs_open);

    return 0;
}

static uint32_t elf_symbol_value(const elf32_symbol_t *symbol,
                                 const char *strings,
                                 const uint32_t *section_addresses,
                                 uint16_t section_count) {
    if (symbol->shndx == SHN_UNDEF)
        return elf_kernel_symbol(strings + symbol->name);
    if (symbol->shndx == SHN_ABS) return symbol->value;
    if (symbol->shndx >= section_count ||
        section_addresses[symbol->shndx] == 0) return 0;
    return section_addresses[symbol->shndx] + symbol->value;
}

static bool elf_validate(const elf32_header_t *header, uint32_t size) {
    if (!header || size < sizeof(*header)) return false;
    if (header->ident[0] != 0x7F || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != 1 || header->ident[5] != 1 ||
        header->type != ET_REL || header->machine != EM_386 ||
        header->version != EV_CURRENT ||
        header->shentsize != sizeof(elf32_section_t) ||
        header->shnum == 0) return false;
    return elf_range_ok(header->shoff,
                        (uint32_t)header->shnum * header->shentsize, size);
}

static bool elf_load(const uint8_t *file, uint32_t file_size,
                     void **image_out, elf_program_entry_t *entry_out) {
    const elf32_header_t *header = (const elf32_header_t *)file;
    const elf32_section_t *sections;
    uint32_t *addresses;
    uint8_t *image;
    uint32_t image_size = 0;
    const elf32_section_t *symtab_section = NULL;
    const elf32_symbol_t *symbols = NULL;
    const char *strings = NULL;
    uint32_t symbol_count = 0;

    if (!elf_validate(header, file_size)) {
        g_elf_error = "ELF32 ET_REL invalido";
        return false;
    }
    sections = (const elf32_section_t *)(file + header->shoff);
    addresses = (uint32_t *)kzalloc(header->shnum * sizeof(uint32_t));
    if (!addresses) {
        g_elf_error = "sin memoria para secciones ELF";
        return false;
    }

    for (uint16_t i = 0; i < header->shnum; i++) {
        if (!(sections[i].flags & SHF_ALLOC)) continue;
        image_size = elf_align(image_size,
                               sections[i].addralign ? sections[i].addralign : 1);
        addresses[i] = image_size;
        if (sections[i].size > 0xFFFFFFFFU - image_size) {
            kfree(addresses);
            g_elf_error = "imagen ELF demasiado grande";
            return false;
        }
        image_size += sections[i].size;
    }
    image = (uint8_t *)kzalloc(image_size);
    if (!image) {
        kfree(addresses);
        g_elf_error = "sin memoria para cargar programa";
        return false;
    }
    for (uint16_t i = 0; i < header->shnum; i++) {
        if (!(sections[i].flags & SHF_ALLOC)) continue;
        addresses[i] += (uint32_t)(uintptr_t)image;
        if (sections[i].type == SHT_NOBITS) continue;
        if (!elf_range_ok(sections[i].offset, sections[i].size, file_size)) {
            g_elf_error = "seccion ELF fuera del archivo";
            goto fail;
        }
        kmemcpy((void *)(uintptr_t)addresses[i],
                file + sections[i].offset, sections[i].size);
    }

    for (uint16_t i = 0; i < header->shnum; i++) {
        if (sections[i].type != SHT_SYMTAB) continue;
        if (!elf_range_ok(sections[i].offset, sections[i].size, file_size) ||
            sections[i].entsize != sizeof(elf32_symbol_t) ||
            sections[i].link >= header->shnum) goto malformed;
        const elf32_section_t *strtab = &sections[sections[i].link];
        if (!elf_range_ok(strtab->offset, strtab->size, file_size)) goto malformed;
        symtab_section = &sections[i];
        symbols = (const elf32_symbol_t *)(file + sections[i].offset);
        strings = (const char *)(file + strtab->offset);
        symbol_count = sections[i].size / sizeof(elf32_symbol_t);
        break;
    }
    if (!symbols || !strings) {
        g_elf_error = "ELF sin tabla de simbolos";
        goto fail;
    }

    for (uint32_t i = 0; i < symbol_count; i++) {
        if (symbols[i].shndx == SHN_UNDEF &&
            symbols[i].name != 0 &&
            elf_symbol_value(&symbols[i], strings, addresses,
                             header->shnum) == 0) {
            kprintf("[ELF] simbolo no resuelto: %s\n", strings + symbols[i].name);
            g_elf_error = "simbolo externo no resuelto";
            goto fail;
        }
    }

    for (uint16_t i = 0; i < header->shnum; i++) {
        const elf32_section_t *relsec = &sections[i];
        if (relsec->type != SHT_REL) continue;
        if (relsec->info >= header->shnum || relsec->link >= header->shnum ||
            &sections[relsec->link] != symtab_section ||
            !elf_range_ok(relsec->offset, relsec->size, file_size) ||
            relsec->entsize != sizeof(elf32_rel_t) ||
            addresses[relsec->info] == 0) goto malformed;

        const elf32_rel_t *rels =
            (const elf32_rel_t *)(file + relsec->offset);
        uint32_t rel_count = relsec->size / sizeof(*rels);
        for (uint32_t r = 0; r < rel_count; r++) {
            uint32_t symbol_index = rels[r].info >> 8;
            uint8_t type = (uint8_t)(rels[r].info & 0xFF);
            uint32_t target_size = sections[relsec->info].size;
            uint32_t *place;
            uint32_t symbol_value;
            if (symbol_index >= symbol_count ||
                rels[r].offset > target_size - sizeof(uint32_t)) goto malformed;
            place = (uint32_t *)(uintptr_t)
                (addresses[relsec->info] + rels[r].offset);
            symbol_value = elf_symbol_value(&symbols[symbol_index], strings,
                                            addresses, header->shnum);
            if (type == R_386_32) {
                *place += symbol_value;
            } else if (type == R_386_PC32) {
                *place += symbol_value - (uint32_t)(uintptr_t)place;
            } else {
                g_elf_error = "tipo de relocacion ELF no soportado";
                goto fail;
            }
        }
    }

    for (uint32_t i = 0; i < symbol_count; i++) {
        if (kstrcmp(strings + symbols[i].name,
                    "bleskernos_program_main") == 0) {
            uint32_t value = elf_symbol_value(&symbols[i], strings, addresses,
                                              header->shnum);
            if (!value) break;
            *image_out = image;
            *entry_out = (elf_program_entry_t)(uintptr_t)value;
            kfree(addresses);
            return true;
        }
    }
    g_elf_error = "falta bleskernos_program_main";
    goto fail;

malformed:
    g_elf_error = "estructura ELF malformada";
fail:
    kfree(image);
    kfree(addresses);
    return false;
}

bool elf_execute_program(const char *path, gui_desktop_t *desktop) {
    void *file = NULL;
    uint32_t size = 0;

    if (!path || !desktop) return false;
    if (g_loaded_entry && kstrcmp(path, g_loaded_path) == 0) {
        g_loaded_entry(desktop);
        return true;
    }
    if (!vfs_read_all(path, &file, &size)) {
        g_elf_error = "no se pudo leer el programa";
        return false;
    }
    if (!elf_load((const uint8_t *)file, size,
                  &g_loaded_image, &g_loaded_entry)) {
        kfree(file);
        return false;
    }
    kfree(file);
    kstrncpy(g_loaded_path, path, sizeof(g_loaded_path) - 1);
    g_loaded_path[sizeof(g_loaded_path) - 1] = '\0';
    kprintf("[ELF] cargado %s\n", path);
    g_loaded_entry(desktop);
    return true;
}

const char *elf_last_error(void) {
    return g_elf_error;
}
