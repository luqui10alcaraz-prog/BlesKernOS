#include "include/types.h"
#include "include/vga.h"
#include "include/memory.h"
#include "include/keyboard.h"
#include "include/pic.h"
#include "include/idt.h"
#include "include/shell.h"
#include "include/pit.h"
#include "include/sound.h"
#include "include/block.h"
#include "include/ata.h"
#include "include/floppy.h"
#include "include/vfs.h"
#include "include/pci.h"
#include "include/gfx.h"
#include "include/mouse.h"
#include "../gui/gui.h"
#include "include/task.h"
#include "include/gdt.h"

extern void usermode_smoke_entry(void);

void kernel_main(void) {
    vga_init();
    gfx_init();
    mm_init();
    gdt_init();
    task_init();
    idt_init();
    pic_init();
    pit_init();
    sound_init();
    kbd_init();
    mouse_init();
    pci_init();
    block_init();
    ata_init();
    floppy_init();
    vfs_init();
    sti();
    task_create_user("ring3-smoke", usermode_smoke_entry);
    task_yield();

    if (!vfs_mount_default()) {
        kprintf("[Kernel] No se pudo montar un volumen FAT.\n");
        kprintf("[Kernel] Iniciando shell de recuperacion.\n");
        shell_run();
    }

    gui_init();
    gui_run();
}
