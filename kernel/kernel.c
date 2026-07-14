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
#include "include/vfs.h"
#include "include/pci.h"
#include "include/gfx.h"
#include "include/mouse.h"
#include "../gui/gui.h"
#include "include/task.h"
#include "include/gdt.h"
#include "include/bootsplash.h"
#include "include/usb_storage.h"
#include "include/recovery_console.h"
#include "include/driver.h"
#include "include/startup_sound.h"
#include "include/elf_loader.h"

static void kernel_ring3_proxy_selftest(void) {
    int pid;
    int32_t status = -1;

    pid = elf_spawn_program_ex("/SYSTEM/PROGRAMS/RING3PROXY.O",
                               (gui_desktop_t *)(uintptr_t)1U, NULL);
    if (pid < 0) {
        kprintf("[RING3] no se pudo cargar autoprueba: %s\n",
                elf_last_error());
        return;
    }
    while (task_waitpid((uint32_t)pid, &status) == 0) task_yield();
    kprintf("[RING3] proxy API y retorno a user-space: %s\n",
            status == 0 ? "OK" : "ERROR");
}

extern void usermode_smoke_entry(void);

void kernel_main(void) {
    vga_init();
    gfx_init();
    bootsplash_show("STARTING KERNEL", 3);
    mm_init();
    bootsplash_show("MEMORY MANAGER", 10);
    gdt_init();
    task_init();
    bootsplash_show("TASK SYSTEM", 18);
    idt_init();
    pic_init();
    pit_init();
    bootsplash_show("INTERRUPTS READY", 28);
    kbd_init();
    bootsplash_show("INPUT DEVICES", 38);
    pci_init();
    block_init();
    usb_storage_init();
    ata_init();
    bootsplash_show("STORAGE DRIVERS", 52);
    vfs_init();
    driver_loader_init();
    bootsplash_show("MOUNTING FILESYSTEM", 62);
    sti();
    usb_storage_start_hotplug_task();
    task_create_user("ring3-smoke", usermode_smoke_entry);
    task_yield();

    if (!vfs_mount_default()) {
        /* El splash usa framebuffer; el shell de recuperacion usa VGA texto. */
        bootsplash_show("RECOVERY SHELL", 100);
        bootsplash_disable();
        recovery_console_enter();
        vga_init();
        kprintf("[Kernel] No se pudo montar un volumen FAT.\n");
        kprintf("[Kernel] Iniciando shell de recuperacion.\n");
        shell_run();
    }

    kernel_ring3_proxy_selftest();

    kprintf("[DVR] Buscando modulos en /SYSTEM/DRIVERS\n");
    kprintf("[DVR] %u modulo(s) cargado(s)\n",
            driver_load_directory("/SYSTEM/DRIVERS"));

    /* El arranque temprano conserva VGA/texto. VESA.DVR ya esta residente y
       ahora puede adjuntar el framebuffer que preparo el bootloader. */
    gfx_init();

    bootsplash_show("STARTING GUI", 70);
    gui_init();
    bootsplash_debug("kernel_main: gui_init returned");
    bootsplash_disable();
    startup_sound_play();
    gui_run();
}
