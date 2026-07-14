#include "common.h"

static int run(int argc, char **argv) {
    uint32_t count = bk_device_block_count();
    uint32_t partitions = bk_device_partition_count();
    uint32_t shown_devices = 0;
    uint32_t shown_partitions = 0;
    const char *filter = argc == 2 ? argv[1] : NULL;
    bool matched = false;
    if (argc > 2) return command_error("fdisk", "uso: fdisk [dispositivo]");
    kprintf("BlesKernOS FDISK - vista segura de discos y tabla MBR\n\n");
    kprintf("DISPOSITIVO TIPO      SECTORES TAM. ESTADO\n");
    for (uint32_t i = 0; i < count; i++) {
        bk_block_info_t device;
        if (!bk_device_block_info(i, &device)) continue;
        if (filter && !command_is(filter, device.name)) continue;
        matched = true;
        shown_devices++;
        kprintf("%-11s %-9s %8u %4u %s%s\n", device.name,
                device.type_name, device.sector_count, device.sector_size,
                device.read_only ? "RO" : "RW",
                device.removable ? " removible" : "");
    }
    if (filter && !matched)
        return command_error("fdisk", "dispositivo no encontrado");

    kprintf("\nPARTICIONES MBR\n");
    kprintf("DISCO       N ARR TIPO               INICIO   SECTORES   MB\n");
    for (uint32_t i = 0; i < partitions; i++) {
        bk_partition_info_t partition;
        if (!bk_device_partition_info(i, &partition) ||
            (filter && !command_is(filter, partition.device_name))) continue;
        shown_partitions++;
        kprintf("%-11s %u  %c  %-18s %8u %9u %5u\n",
                partition.device_name, partition.table_index + 1U,
                partition.bootable ? '*' : '-', partition.type_name,
                partition.first_sector, partition.sector_count,
                (uint32_t)(partition.size_bytes / (1024U * 1024U)));
    }
    if (!shown_partitions)
        kprintf("No hay particiones MBR validas; el disco puede usar FAT directo.\n");
    kprintf("\n%u dispositivo(s), %u particion(es). Modo de consulta: no se escribio el MBR.\n",
            shown_devices, shown_partitions);
    return 0;
}

BK_COMMAND_MAIN(run)
