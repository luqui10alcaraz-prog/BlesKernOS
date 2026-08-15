#include "include/types.h"
#include "include/vga.h"
#include "include/memory.h"
#include "include/paging.h"
#include "include/keyboard.h"
#include "include/pic.h"
#include "include/idt.h"
#include "include/shell.h"
#include "include/pit.h"
#include "include/sound.h"
#include "include/block.h"
#include "include/ata.h"
#include "include/vfs.h"
#include "include/iso9660.h"
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
#include "include/graphics_resources.h"
#include "include/startup_sound.h"
#include "include/elf_loader.h"
#include "include/network.h"
#include "include/language.h"
#include "include/boot_mode.h"
#include "include/installer.h"
#include "include/floppy.h"
#include "include/floppy_installer.h"
#include "include/bkl_setup.h"
#include "include/setup_boot.h"
#include "include/lpt.h"
#include "include/api.h"
#include "include/perfmon.h"
#include "include/compat_mode.h"
#include "include/smp.h"
#include "include/smp_watchdog.h"
#include "include/kernel_domains.h"

static bool kernel_start_print_spooler(void) {
    vfs_dir_entry_t service;
    int pid;

    if (!vfs_stat("/SYSTEM/SERVICES/PRINTSPL.BEX", &service) ||
        service.type != VFS_NODE_FILE) {
        kprintf("[PRINT] PRINTSPL.BEX no encontrado; impresion desactivada\n");
        return false;
    }

    bk_print_spooler_set_ready(false);
    pid = elf_spawn_program_ex("/SYSTEM/SERVICES/PRINTSPL.BEX",
                               (gui_desktop_t *)(uintptr_t)1U, NULL);
    if (pid < 0) {
        kprintf("[PRINT] no se pudo iniciar PRINTSPL.BEX: %s\n",
                elf_last_error());
        return false;
    }

    /* La impresión no es una dependencia para construir el escritorio.
       Esperar aquí hasta cinco segundos detenía todo el boot y aun podía
       vencer justo antes de que el proceso Ring 3 marcara READY. El daemon
       publica su estado por la misma API; las operaciones de impresión pueden
       consultarlo sin serializar el arranque gráfico. */
    kprintf("[PRINT] spooler iniciado en segundo plano, pid=%d\n", pid);
    return true;
}


static void kernel_ring3_proxy_selftest(void) {
    int pid;
    int32_t status = -1;

    pid = elf_spawn_program_ex("/SYSTEM/PROGRAMS/RING3PROXY.BEX",
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


void kernel_main(void) {
    bool installer_boot = boot_mode_is_installer();
    bool graphical_setup_boot = false;
    vga_init();
    /* No dejar VESA, E820 ni paging delante de los exception handlers. En
     * hardware real una excepcion aqui antes terminaba como triple fault. */
    gdt_init();
    idt_init();
    gfx_init();
    bootsplash_show("STARTING KERNEL", 3);
    /* Stage 2 leaves the E820 map at physical 0x500, inside page zero.
     * Preserve it before paging deliberately unmaps that page. */
    mm_boot_snapshot();
    (void)paging_init();
    mm_init();
    compat_mode_init();
    bootsplash_show("MEMORY MANAGER", 10);
    task_init();
    idt_enable_runtime_handlers();
    kernel_domains_init();
    bootsplash_show("TASK SYSTEM", 18);
    pic_init();
    pit_init();
    smp_watchdog_init();
    perfmon_init();
    bootsplash_show("INTERRUPTS READY", 28);
    kbd_init();
    bootsplash_show("INPUT DEVICES", 38);
    pci_init();
    lpt_init();
    block_init();
    usb_storage_init();
    ata_init();
    if (installer_boot) floppy_init();
    bootsplash_show("STORAGE DRIVERS", 52);
    vfs_init();
    driver_loader_init();
    bootsplash_show("MOUNTING FILESYSTEM", 62);
    sti();

    /* El Stage 1 de instalacion escribe el magic INST. Primero se intenta
       montar un CD ISO9660; si no existe, Setup entra en modo multidisquete. */
    if (installer_boot) {
        usb_storage_start_hotplug_task();
        bootsplash_disable();
        if (iso9660_builtin_init() && iso9660_mount_default()) {
            kprintf("[SETUP] Instalador CD ISO9660 detectado.\n");
            vfs_use_cdrom_as_root(true);
            installer_run();
        } else {
            kprintf("[SETUP] Sin CD ISO9660; usando instalador en disquetes.\n");
            vfs_use_cdrom_as_root(false);
            floppy_installer_run();
        }
        return;
    }

    if (compat_mode_allow_optional_services())
        usb_storage_start_hotplug_task();
    if (compat_mode_allow_user_programs() &&
        compat_mode_allow_optional_services()) {
        /* The old smoke test entered a kernel-resident function at CPL3.
         * Supervisor-only kernel text correctly rejects that after Phase 1,
         * so it was only generating a false boot failure.  Real ELF loading
         * below is now the Ring-3 validation path. */
        task_yield();
    }

    if (!vfs_mount_default()) {
        /* Live CD: el kernel trae un lector ISO9660 minimo integrado para
           romper el ciclo driver-en-CD / CD-sin-driver. El resto de drivers
           y programas se cargan normalmente desde la raiz ISO de solo lectura. */
        kprintf("[BOOT] FAT no disponible; intentando Live CD ISO9660...\n");
        if (iso9660_builtin_init() && iso9660_mount_default()) {
            vfs_use_cdrom_as_root(true);
            kprintf("[BOOT] Live CD montado como raiz de solo lectura.\n");
        } else {
            /* El splash usa framebuffer; el shell de recuperacion usa VGA texto. */
            bootsplash_show("RECOVERY SHELL", 100);
            bootsplash_disable();
            recovery_console_enter();
            vga_init();
            kprintf("[Kernel] No se pudo montar FAT ni ISO9660.\n");
            kprintf("[Kernel] Iniciando shell de recuperacion.\n");
            shell_run();
        }
    }

    if (bkl_setup_pending()) {
        bootsplash_disable();
        vga_init();
        kprintf("[BKL] Instalacion pendiente detectada; extrayendo paquete.\n");
        if (!bkl_setup_finalize()) {
            kprintf("[BKL] Error al finalizar la instalacion.\n");
            kprintf("Conserve /SETUP para diagnostico y reinicie.\n");
            for (;;) __asm__ volatile ("hlt");
        }
    }

    graphical_setup_boot = setup_boot_requested();
    if (graphical_setup_boot)
        kprintf("[SETUP] START.INI solicita el asistente de primer arranque.\n");

    language_init();

    if (compat_mode_allow_optional_services()) {
        kernel_ring3_proxy_selftest();
        kprintf("[DVR] Buscando modulos en /SYSTEM/DRIVERS\n");
        kprintf("[DVR] %u modulo(s) cargado(s)\n",
                driver_load_directory("/SYSTEM/DRIVERS"));
        network_start_autoconfigure();
    } else {
        /* Los módulos que no detectan su hardware se liberan inmediatamente
         * en driver_load(). Con vfs_read_all sin doble cache, cargar la tabla
         * completa cuesta sólo las imágenes de los controladores útiles. */
        kprintf("[COMPAT] cargando drivers completos; servicios pesados bajo demanda\n");
        kprintf("[DVR] %u modulo(s) cargado(s) en perfil de 8 MiB\n",
                driver_load_directory("/SYSTEM/DRIVERS"));
    }

    /* El volumen principal sigue siendo FAT; un CD de datos es secundario.
       Móntelo una única vez ahora que ISO9660.DVR está registrado, nunca desde
       el compositor o un callback Ring 3. Eso hace visible /CDROM para el
       navegador sin introducir I/O ATAPI durante su apertura. */
    if (!vfs_has_cdrom() && iso9660_mount_default())
        kprintf("[VFS] CD-ROM ISO9660 montado en /CDROM\n");

    /* El arranque temprano conserva VGA/texto. Con PCI ya enumerado, esta
       segunda pasada prioriza VMware SVGA-II y conserva VESA como fallback. */
    gfx_init();
    {
        const gfx_info_t *boot_graphics = gfx_get_info();
        bool keep_default_256 = boot_graphics &&
            boot_graphics->width == 640U &&
            boot_graphics->height == 480U &&
            boot_graphics->bpp == 8U;

        if (!keep_default_256 && compat_mode_force_vga13h() && !gfx_set_mode13h())
        kprintf("[COMPAT] no se pudo forzar VGA 320x200x8\n");
        else if (!keep_default_256 && compat_mode_force_vga12h() && !gfx_set_mode12h())
        kprintf("[COMPAT] no se pudo forzar VGA 640x480x4\n");
    }
    /* El splash vive en VRAM y no mantiene un backbuffer adicional: también
       se conserva durante el arranque del perfil de 8 MiB. */

    /* GRAPHICS.PAK se leía por primera vez mientras PrintSpool todavía
       terminaba su arranque Ring 3. Precargar aquí hace determinista el
       paquete y su caché antes de permitir I/O concurrente de servicios. */
    if (compat_mode_allow_icon_images())
        (void)bk_graphics_preload_boot_icons();
    gui_alert_resources_init();

    /* Inicie PrintSpool sin bloquear la construcción del escritorio. */
    if (compat_mode_allow_optional_services()) {
        bootsplash_show("LOADING PRINT SERVICE", 68);
        (void)kernel_start_print_spooler();
    } else {
        kprintf("[COMPAT] PrintSpool omitido para ahorrar RAM\n");
    }

    bootsplash_show("STARTING GUI", 70);
    gui_set_setup_mode(graphical_setup_boot);
    gui_init();
    bootsplash_debug("kernel_main: gui_init returned");
    if (compat_mode_is_low_memory()) {
        heap_info_t heap;
        mm_get_info(&heap);
        kprintf("[COMPAT] escritorio listo: heap libre=%u KiB usado=%u KiB bloques=%u\n",
                (uint32_t)(heap.free_bytes / 1024U),
                (uint32_t)(heap.used_bytes / 1024U), heap.used_blocks);
    }
    bootsplash_disable();
    if (graphical_setup_boot) {
        if (!setup_boot_launch())
            kprintf("[SETUP] El asistente grafico no pudo iniciarse.\n");
    } else if (compat_mode_allow_startup_sound()) {
        kprintf("[BKBOOT] QUEUE STARTUP SOUND\n");
        startup_sound_play();
    } else {
        kprintf("[COMPAT] sonido de inicio omitido\n");
    }

    /*
     * Arranque SMP tardio:
     *
     * La primera implementacion encendia el Local APIC y dejaba los AP
     * esperando antes de montar FAT, cargar ICONS.PAK y construir el
     * escritorio. Ademas, task_preempt_* consultaba el APIC ID por MMIO en
     * cada operacion de heap/FAT. En KVM -cpu host esa combinacion podia
     * convertir la precarga de iconos en una pausa aparente o permanente.
     *
     * La inicializacion del escritorio es necesariamente monohilo. Iniciar
     * los AP justo antes del bucle GUI conserva el boot probado de un solo
     * CPU y activa el paralelismo donde realmente puede utilizarse.
     */
    if (!compat_mode_is_low_memory()) {
        kprintf("[SMP] escritorio listo; iniciando procesadores secundarios\n");
        smp_init();
        smp_start_scheduler();
    } else {
        kprintf("[SMP] desactivado por perfil de memoria reducido\n");
    }
    kprintf("[BKBOOT] ENTER GUI LOOP\n");
    /* TEMP_FREECIV_LOADING_TEST */
    (void)bk_app_launch("/ata2/SYSTEM/PROGRAMS/FREECIV.O", NULL);
    gui_run();
}
