#include "common.h"

static const char g_autoexec[] =
    "@ECHO OFF\r\n"
    "ECHO BOS RECOVERY ENVIRONMENT\r\n"
    "ECHO BLESKERNOS CONTINUITY TOOLS\r\n"
    "DIAG.CMD\r\n";

static const char g_diag[] =
    "ECHO CHECKING MEMORY...\r\n"
    "MEM\r\n"
    "ECHO CHECKING DISKS...\r\n"
    "FDISK\r\n"
    "CHECKDISK /READONLY\r\n"
    "ECHO CHECKING DEVICES...\r\n"
    "PCI\r\n"
    "ECHO DIAGNOSTICS COMPLETE\r\n";

static const char g_config[] =
    "; BOS recovery configuration imported by BlesKernOS\r\n"
    "NODE=BOSRECOVERY\r\n"
    "NETWORK=OFF\r\n"
    "SAFE_MODE=ON\r\n";

static const char g_readme[] =
    "BOS RECOVERY DISK\r\n"
    "Created by BlesKernOS BOS Compatibility Environment.\r\n"
    "Boot BOS from a known-good disk, then run AUTOEXEC.CMD or DIAG.CMD.\r\n"
    "BlesKernOS mounts this legacy FAT12 disk as /fd0.\r\n";

static bool store(const char *path, const char *text)
{
    return bk_file_write_all(path, text, (uint32_t)bk_runtime_strlen(text));
}

static int run(int argc, char **argv)
{
    if (argc != 2 || !command_is(argv[1], "/YES")) {
        bk_console_write("Uso: bosrecovery /YES\n");
        bk_console_write("Crea un disquete BOS de diagnostico en fd0.\n");
        bk_console_write("ADVERTENCIA: borra todo el contenido del disquete.\n");
        return 2;
    }
    bk_console_write("Insertando medio: creando BOS Recovery Disk...\n");
    if (!bk_device_format_fat("fd0", "BOSRECOV"))
        return command_error("bosrecovery", "no se pudo formatear fd0");
    if (!bk_device_mount_volume("fd0"))
        return command_error("bosrecovery", "no se pudo montar fd0");
    if (!store("/fd0/AUTOEXEC.CMD", g_autoexec) ||
        !store("/fd0/DIAG.CMD", g_diag) ||
        !store("/fd0/SYSTEM.CFG", g_config) ||
        !store("/fd0/README.TXT", g_readme))
        return command_error("bosrecovery", "no se pudieron copiar los archivos");
    bk_console_write("BOS Recovery Disk creado correctamente en fd0.\n");
    bk_console_write("Contiene AUTOEXEC.CMD, DIAG.CMD y SYSTEM.CFG.\n");
    return 0;
}

BK_COMMAND_MAIN(run)
