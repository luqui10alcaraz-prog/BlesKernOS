#include "common.h"

static int run(int argc UNUSED, char **argv UNUSED) {
    bk_volume_info_t volume;
    bk_volume_check_report_t check;
    if (!bk_device_volume_info(&volume))
        return command_error("fsinfo", "no hay un volumen FAT montado");
    kprintf("Volumen:      %s\n", volume.mount_name);
    kprintf("Dispositivo:  %s (%s)\n", volume.device_name,
            volume.read_only ? "solo lectura" : "lectura/escritura");
    kprintf("Sistema:      FAT%u (%s)\n", volume.fat_bits,
            volume.filesystem);
    kprintf("Etiqueta:     %s\n", volume.volume_label);
    kprintf("Sector:       %u bytes\n", volume.bytes_per_sector);
    kprintf("Cluster:      %u bytes\n",
            (uint32_t)volume.bytes_per_sector * volume.sectors_per_cluster);
    kprintf("Clusters:     %u\n", volume.total_clusters);
    kprintf("Capacidad:    %u KB\n", (uint32_t)(volume.total_bytes / 1024U));
    kprintf("Libre:        %u KB\n", (uint32_t)(volume.free_bytes / 1024U));
    kprintf("Ocupado:      %u KB\n",
            (uint32_t)((volume.total_bytes - volume.free_bytes) / 1024U));
    kprintf("\nAnalizando asignacion FAT...\n");
    if (!bk_device_check_volume(&check))
        return command_error("fsinfo", "no se pudo analizar la estructura FAT");
    kprintf("Archivos:     %u\n", check.files);
    kprintf("Directorios:  %u\n", check.directories);
    kprintf("Fragmentados: %u archivo(s), %u fragmento(s)\n",
            check.fragmented_files, check.total_fragments);
    kprintf("Mayor bloque libre: %u clusters (%u KB)\n",
            check.largest_free_run,
            (check.largest_free_run * volume.bytes_per_sector *
             volume.sectors_per_cluster) / 1024U);
    kprintf("Perdidos/cruzados: %u / %u\n", check.lost_clusters,
            check.crosslinked_clusters);
    kprintf("Copias FAT:   %s\n",
            check.fat_copies_match ? "coinciden" : "DIFERENTES");
    kprintf("Estado:       %s (%u errores, %u advertencias)\n",
            check.errors ? "REQUIERE ATENCION" : "correcto",
            check.errors, check.warnings);
    return 0;
}

BK_COMMAND_MAIN(run)
