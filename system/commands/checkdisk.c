#include "common.h"

#define CHECKDISK_DEPTH_MAX 16U

typedef struct {
    uint32_t directories;
    uint32_t files;
    uint32_t errors;
    uint64_t bytes;
} checkdisk_report_t;

static void join_path(char *output, uint32_t capacity, const char *directory,
                      const char *name) {
    uint32_t used;
    bk_runtime_strncpy(output, directory && directory[0] ? directory : "/",
                       capacity - 1U);
    output[capacity - 1U] = '\0';
    used = (uint32_t)bk_runtime_strlen(output);
    if (used && output[used - 1U] != '/' && used + 1U < capacity) {
        output[used++] = '/';
        output[used] = '\0';
    }
    if (name && used + bk_runtime_strlen(name) < capacity)
        bk_runtime_strcat(output, name);
}

static bool dot_entry(const char *name) {
    return command_is(name, ".") || command_is(name, "..");
}

static void scan_directory(const char *path, uint32_t depth,
                           checkdisk_report_t *report) {
    bk_file_entry_t *entries;
    uint32_t count = 0;

    if (depth > CHECKDISK_DEPTH_MAX) {
        kprintf("ERROR: profundidad excesiva en %s\n", path);
        report->errors++;
        return;
    }
    entries = (bk_file_entry_t *)bk_sys_alloc(
        sizeof(bk_file_entry_t) * BK_DIRECTORY_MAX);
    if (!entries) {
        kprintf("ERROR: memoria insuficiente al analizar %s\n", path);
        report->errors++;
        return;
    }
    if (!bk_file_list_dir(path, entries, BK_DIRECTORY_MAX, &count)) {
        kprintf("ERROR: no se puede leer el directorio %s\n", path);
        report->errors++;
        bk_sys_free(entries);
        return;
    }
    report->directories++;
    if (count == BK_DIRECTORY_MAX)
        kprintf("AVISO: %s alcanzo el limite de %u entradas de la API\n",
                path, BK_DIRECTORY_MAX);

    for (uint32_t i = 0; i < count; i++) {
        char child[BK_PATH_MAX];
        void *contents = NULL;
        uint32_t size = 0;

        if (dot_entry(entries[i].name)) continue;
        join_path(child, sizeof(child), path, entries[i].name);
        if (entries[i].type == BK_FILE_NODE_DIRECTORY) {
            scan_directory(child, depth + 1U, report);
            continue;
        }

        report->files++;
        report->bytes += entries[i].size;
        if (!bk_file_read_all(child, &contents, &size)) {
            kprintf("ERROR: no se puede leer %s\n", child);
            report->errors++;
            continue;
        }
        if (size != entries[i].size) {
            kprintf("ERROR: tamano de %s: directorio=%u lectura=%u\n",
                    child, entries[i].size, size);
            report->errors++;
        }
        bk_sys_free(contents);
    }
    bk_sys_free(entries);
}

static int run(int argc, char **argv) {
    const char *path = "/";
    bool fix = false;
    bool path_set = false;
    bk_volume_info_t volume;
    bk_volume_check_report_t structure;
    bk_volume_repair_report_t repair;
    checkdisk_report_t report = {0, 0, 0, 0};

    for (int i = 1; i < argc; i++) {
        if (command_is(argv[i], "/fix") || command_is(argv[i], "-f")) {
            if (fix) return command_error("checkdisk", "opcion /fix repetida");
            fix = true;
        } else if (!path_set) {
            path = argv[i];
            path_set = true;
        } else {
            return command_error("checkdisk",
                                 "uso: checkdisk [/fix] [directorio]");
        }
    }
    if (!bk_device_volume_info(&volume))
        return command_error("checkdisk", "no hay volumen FAT montado");

    kprintf("BlesKernOS CheckDisk - analisis FAT de solo lectura\n");
    kprintf("Volumen %s, FAT%u, dispositivo %s\n", volume.volume_label,
            volume.fat_bits, volume.device_name);
    kprintf("\n[1/2] Comprobando estructura FAT...\n");
    if (!bk_device_check_volume(&structure)) {
        kprintf("No se pudo completar el analisis estructural.\n");
        kprintf("Sector de arranque: %s\n",
                structure.boot_sector_valid ? "valido" : "INVALIDO");
        kprintf("Errores de E/S: %u\n", structure.io_errors);
        return 2;
    }
    kprintf("Sector de arranque:      %s\n",
            structure.boot_sector_valid ? "valido" : "INVALIDO");
    kprintf("Copia de arranque:       %s\n",
            structure.backup_boot_matches ? "coincide" : "diferente/no disponible");
    kprintf("Copias de la FAT:        %s\n",
            structure.fat_copies_match ? "coinciden" : "DIFERENTES");
    kprintf("Archivos/directorios:    %u / %u\n", structure.files,
            structure.directories);
    kprintf("Clusters usados/libres:  %u / %u\n",
            structure.allocated_clusters, structure.free_clusters);
    kprintf("Clusters referenciados:  %u\n", structure.referenced_clusters);
    kprintf("Clusters perdidos:       %u\n", structure.lost_clusters);
    kprintf("Clusters cruzados:       %u\n", structure.crosslinked_clusters);
    kprintf("Cadenas circulares:      %u\n", structure.circular_chains);
    kprintf("Cadenas invalidas:       %u\n", structure.invalid_chains);
    kprintf("Tamanos incoherentes:    %u\n", structure.size_mismatches);
    kprintf("Errores de directorio:   %u\n", structure.directory_errors);
    kprintf("Sectores FAT distintos:  %u\n", structure.fat_mismatch_sectors);
    kprintf("Clusters malos:          %u\n", structure.bad_clusters);
    kprintf("Clusters reservados:     %u\n", structure.reserved_clusters);
    kprintf("Archivos fragmentados:   %u (%u fragmentos)\n",
            structure.fragmented_files, structure.total_fragments);
    kprintf("Mayor bloque libre:      %u clusters\n",
            structure.largest_free_run);
    kprintf("Errores de E/S FAT:      %u\n", structure.io_errors);

    if (fix) {
        bk_volume_check_report_t after;
        kprintf("\n[REPARACION] Aplicando correcciones controladas...\n");
        if (volume.read_only) {
            kprintf("ERROR: el volumen esta montado en modo solo lectura.\n");
            return 2;
        }
        if (!bk_device_repair_volume(&repair, &after)) {
            kprintf("ERROR: la reparacion no pudo completarse.\n");
            if (repair.scan_incomplete)
                kprintf("Motivo: el arbol de directorios no pudo recorrerse entero; no se escribio nada.\n");
            kprintf("Errores de escritura:    %u\n", repair.write_errors);
            return 2;
        }
        kprintf("Copias FAT sincronizadas:%s\n",
                repair.fat_copies_synchronized ? " si" : " no");
        kprintf("Copia de arranque:       %s\n",
                repair.backup_boot_repaired ? "correcta" : "sin reparar");
        kprintf("Cadenas truncadas:       %u\n", repair.chains_truncated);
        kprintf("Clusters huerfanos libres:%u\n",
                repair.lost_clusters_freed);
        kprintf("Cruces no reparables:    %u\n",
                repair.unrepaired_crosslinks);
        kprintf("Errores antes/despues:   %u / %u\n",
                repair.errors_before, repair.errors_after);
        structure = after;
    }

    kprintf("\n[2/2] Leyendo archivos desde %s...\n", path);
    scan_directory(path, 0, &report);

    kprintf("Directorios leidos:      %u\n", report.directories);
    kprintf("Archivos leidos:         %u\n", report.files);
    kprintf("Datos comprobados:       %u KB\n",
            (uint32_t)(report.bytes / 1024U));
    kprintf("Errores de lectura:      %u\n", report.errors);
    kprintf("\nErrores estructurales:   %u\n", structure.errors);
    kprintf("Advertencias:            %u\n", structure.warnings);
    if (report.errors || structure.errors) {
        kprintf("RESULTADO: se encontraron problemas en el volumen.\n");
        return 1;
    }
    kprintf("RESULTADO: el volumen no presenta errores detectables.\n");
    return 0;
}

BK_COMMAND_MAIN(run)
