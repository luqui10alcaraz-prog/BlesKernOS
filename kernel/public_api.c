#include "include/inflate.h"
#include "include/types.h"
#define BK_API_NO_LEGACY_ALIASES
#include "../sdk/include/bleskernos_api.h"
#include "include/block.h"
#include "include/driver.h"
#include "include/fat.h"
#include "include/memory.h"
#include "include/network.h"
#include "include/pci.h"
#include "include/perfmon.h"
#include "include/task.h"
#include "include/language.h"
#include "include/vfs.h"
#include "include/vga.h"
#include "stdio.h"

typedef char public_perf_snapshot_layout_must_match[
    sizeof(bk_perf_snapshot_t) == sizeof(perfmon_snapshot_t) ? 1 : -1];
typedef char public_perf_benchmark_layout_must_match[
    sizeof(bk_perf_benchmark_t) == sizeof(perfmon_benchmark_result_t) ? 1 : -1];

/*
 * Adaptadores de la ABI publica. Este es el unico lugar donde los DTO del SDK
 * conocen las estructuras internas necesarias para producir una copia segura.
 */

static void public_copy_text(char *destination, uint32_t capacity,
                             const char *source) {
    if (!destination || !capacity) return;
    kstrncpy(destination, source ? source : "", capacity - 1U);
    destination[capacity - 1U] = '\0';
}

void bk_console_printf(const char *format, ...) {
    char buffer[1024];
    va_list arguments;
    if (!format) return;
    format = language_translate(format);
    va_start(arguments, format);
    (void)vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    vga_puts(buffer);
}

size_t bk_string_length(const char *text) {
    return kstrlen(text ? text : "");
}

int bk_string_compare(const char *left, const char *right) {
    return kstrcmp(left ? left : "", right ? right : "");
}

char *bk_string_copy_n(char *destination, const char *source,
                       size_t capacity) {
    return kstrncpy(destination, source ? source : "", capacity);
}

char *bk_string_concat(char *destination, const char *source) {
    return kstrcat(destination, source ? source : "");
}

int bk_memory_compare(const void *left, const void *right, size_t size) {
    return kmemcmp(left, right, size);
}

bool bk_proc_launch_arg_copy(char *buffer, uint32_t capacity) {
    if (!buffer || !capacity) return false;
    public_copy_text(buffer, capacity, task_launch_arg());
    return true;
}


bool bk_net_get_info(bk_net_info_t *info) {
    net_info_t internal;
    if (!info) return false;
    kmemset(&internal, 0, sizeof(internal));
    kmemset(info, 0, sizeof(*info));
    network_get_info(&internal);
    info->configured = internal.configured;
    info->link_up = internal.link_up;
    info->stack_ready = internal.stack_ready;
    info->tls_ready = internal.tls_ready;
    public_copy_text(info->device, sizeof(info->device), internal.device);
    info->mtu = internal.mtu;
    kmemcpy(info->mac, internal.mac, sizeof(info->mac));
    kmemcpy(info->address, internal.address, sizeof(info->address));
    kmemcpy(info->netmask, internal.netmask, sizeof(info->netmask));
    kmemcpy(info->gateway, internal.gateway, sizeof(info->gateway));
    kmemcpy(info->dns, internal.dns, sizeof(info->dns));
    info->rx_packets = internal.rx_packets;
    info->tx_packets = internal.tx_packets;
    info->rx_dropped = internal.rx_dropped;
    return true;
}

bool bk_net_dhcp(uint32_t timeout_ms) {
    return network_dhcp(timeout_ms);
}

bool bk_net_ping(const uint8_t address[4], uint32_t timeout_ms,
                 uint32_t *round_trip_ms) {
    return network_ping(address, timeout_ms, round_trip_ms);
}

bool bk_net_resolve(const char *hostname, uint8_t address[4],
                    uint32_t timeout_ms) {
    return network_resolve(hostname, address, timeout_ms);
}

int bk_net_socket_open(uint8_t type) {
    return network_socket_open(type);
}

bool bk_net_socket_connect(int socket, const uint8_t address[4],
                           uint16_t port, uint32_t timeout_ms) {
    bool connected = network_socket_connect(socket, address, port, timeout_ms);
    if (!connected) (void)bk_gui_network_error("conectar con el servidor", -1002);
    return connected;
}

bool bk_net_socket_bind(int socket, const uint8_t address[4], uint16_t port) {
    bool bound = network_socket_bind(socket, address, port);
    if (!bound) (void)bk_gui_network_error("abrir el puerto local", -1003);
    return bound;
}

bool bk_net_socket_listen(int socket, uint8_t backlog) {
    bool listening = network_socket_listen(socket, backlog);
    if (!listening) (void)bk_gui_network_error("escuchar conexiones", -1004);
    return listening;
}

int bk_net_socket_accept(int socket, uint8_t address[4], uint16_t *port,
                         uint32_t timeout_ms) {
    return network_socket_accept(socket, address, port, timeout_ms);
}

int32_t bk_net_socket_send(int socket, const void *data, uint32_t length,
                           uint32_t timeout_ms) {
    return network_socket_send(socket, data, length, timeout_ms);
}

int32_t bk_net_socket_receive(int socket, void *data, uint32_t capacity,
                              uint32_t timeout_ms) {
    return network_socket_receive(socket, data, capacity, timeout_ms);
}

int32_t bk_net_socket_sendto(int socket, const uint8_t address[4], uint16_t port,
                             const void *data, uint32_t length,
                             uint32_t timeout_ms) {
    return network_socket_sendto(socket, address, port, data, length,
                                 timeout_ms);
}

int32_t bk_net_socket_receivefrom(int socket, uint8_t address[4], uint16_t *port,
                                  void *data, uint32_t capacity,
                                  uint32_t timeout_ms) {
    return network_socket_receivefrom(socket, address, port, data, capacity,
                                      timeout_ms);
}

bool bk_net_socket_readable(int socket) {
    return network_socket_readable(socket);
}

void bk_net_socket_close(int socket) {
    network_socket_close(socket);
}

int32_t bk_net_http_get(const char *url, void *data, uint32_t capacity,
                        uint32_t timeout_ms) {
    int32_t result;
    if (!url) return -1;
    if (kstrncmp(url, "https://", 8) == 0)
        result = network_https_get(url, data, capacity, timeout_ms);
    else result = network_http_get(url, data, capacity, timeout_ms);
    if (result < 0) (void)bk_gui_network_error("descargar datos", result);
    return result;
}

int32_t bk_net_http_exchange(const char *url, const void *request,
                             uint32_t request_length, void *data,
                             uint32_t capacity, uint32_t timeout_ms) {
    if (!url || !request || !request_length) return -1;
    int32_t result;
    if (kstrncmp(url, "https://", 8) == 0)
        result = network_https_exchange(url, request, request_length, data,
                                        capacity, timeout_ms);
    else result = network_http_exchange(url, request, request_length, data,
                                        capacity, timeout_ms);
    if (result < 0) (void)bk_gui_network_error("intercambiar datos", result);
    return result;
}

int32_t bk_net_tls_last_error(void) {
    return network_tls_last_error();
}

int32_t bk_data_inflate_zlib(const void *input, uint32_t input_length,
                             void *output, uint32_t output_capacity) {
    return inflate_zlib(input, input_length, output, output_capacity);
}

int32_t bk_data_inflate_gzip(const void *input, uint32_t input_length,
                             void *output, uint32_t output_capacity) {
    return inflate_gzip(input, input_length, output, output_capacity);
}

uint32_t bk_proc_cpu_usage(void) {
    return task_cpu_usage();
}

uint32_t bk_proc_cpu_count(void) {
    return task_cpu_count();
}

uint32_t bk_proc_cpu_usage_core(uint32_t cpu_index) {
    return task_cpu_usage_for(cpu_index);
}

uint32_t bk_proc_current_cpu(void) {
    return task_current_cpu();
}

int32_t bk_proc_set_affinity(uint32_t pid, uint32_t cpu_mask) {
    return task_set_affinity_mask(pid, cpu_mask) ? 0 : -1;
}

uint32_t bk_proc_get_affinity(uint32_t pid) {
    return task_get_affinity_mask(pid);
}

uint32_t bk_proc_runqueue_depth(uint32_t cpu_index) {
    return task_runqueue_depth(cpu_index);
}

uint32_t bk_proc_scheduler_steals(uint32_t cpu_index) {
    return task_scheduler_steals(cpu_index);
}

uint32_t bk_proc_scheduler_migrations(uint32_t cpu_index) {
    return task_scheduler_migrations(cpu_index);
}

uint32_t bk_proc_scheduler_ipis(uint32_t cpu_index) {
    return task_scheduler_ipis(cpu_index);
}

bool bk_perf_enabled(void) {
    return perfmon_enabled();
}

void bk_perf_set_enabled(bool enabled) {
    perfmon_set_enabled(enabled);
}

void bk_perf_reset(void) {
    perfmon_reset();
}

bool bk_perf_snapshot(bk_perf_snapshot_t *snapshot, bool reset_interval) {
    return perfmon_capture_snapshot((perfmon_snapshot_t *)snapshot,
                                    reset_interval);
}

int bk_perf_benchmark(bk_perf_benchmark_t *result) {
    return perfmon_run_benchmark_capture(
        (perfmon_benchmark_result_t *)result);
}

uint32_t bk_device_block_count(void) {
    return block_count();
}

bool bk_device_block_info(uint32_t index, bk_block_info_t *info) {
    const block_device_t *device;
    if (!info) return false;
    device = block_at(index);
    if (!device) return false;
    kmemset(info, 0, sizeof(*info));
    public_copy_text(info->name, sizeof(info->name), device->name);
    public_copy_text(info->type_name, sizeof(info->type_name),
                     block_type_name(device->type));
    info->type = (bk_block_type_t)device->type;
    info->sector_count = device->sector_count;
    info->sector_size = device->sector_size;
    info->read_only = device->read_only;
    info->removable = device->type == BLOCK_DEVICE_FLOPPY ||
                      device->type == BLOCK_DEVICE_ATAPI ||
                      device->type == BLOCK_DEVICE_USB;
    return true;
}

static uint32_t public_read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static const char *public_partition_type_name(uint8_t type) {
    switch (type) {
        case 0x01: return "FAT12";
        case 0x04: return "FAT16 <32M";
        case 0x05: return "Extendida CHS";
        case 0x06: return "FAT16";
        case 0x07: return "HPFS/NTFS/exFAT";
        case 0x0B: return "FAT32 CHS";
        case 0x0C: return "FAT32 LBA";
        case 0x0E: return "FAT16 LBA";
        case 0x0F: return "Extendida LBA";
        case 0x82: return "Linux swap";
        case 0x83: return "Linux";
        case 0xEE: return "GPT protectora";
        default: return "Desconocida";
    }
}

static bool public_partition_lookup(uint32_t wanted,
                                    bk_partition_info_t *info,
                                    uint32_t *total) {
    uint32_t ordinal = 0;
    if (total) *total = 0;
    for (uint32_t device_index = 0; device_index < block_count();
         device_index++) {
        block_device_t *device = block_at(device_index);
        uint8_t sector[BLOCK_SECTOR_SIZE];
        if (!device || device->sector_size != BLOCK_SECTOR_SIZE ||
            !block_read(device, 0, 1, sector) || sector[510] != 0x55U ||
            sector[511] != 0xAAU) continue;
        for (uint8_t table_index = 0; table_index < 4U; table_index++) {
            const uint8_t *entry = sector + 446U + table_index * 16U;
            uint32_t first = public_read_le32(entry + 8);
            uint32_t count = public_read_le32(entry + 12);
            bool valid_status = entry[0] == 0x00U || entry[0] == 0x80U;
            if (!valid_status || entry[4] == 0U || !count ||
                first >= device->sector_count ||
                count > device->sector_count - first) continue;
            if (ordinal == wanted && info) {
                kmemset(info, 0, sizeof(*info));
                public_copy_text(info->device_name,
                                 sizeof(info->device_name), device->name);
                info->table_index = table_index;
                info->bootable = entry[0] == 0x80U;
                info->type = entry[4];
                public_copy_text(info->type_name, sizeof(info->type_name),
                                 public_partition_type_name(entry[4]));
                info->first_sector = first;
                info->sector_count = count;
                info->size_bytes = (uint64_t)count * device->sector_size;
            }
            ordinal++;
        }
    }
    if (total) *total = ordinal;
    return wanted < ordinal;
}

uint32_t bk_device_partition_count(void) {
    uint32_t total = 0;
    (void)public_partition_lookup(0xFFFFFFFFU, NULL, &total);
    return total;
}

bool bk_device_partition_info(uint32_t index, bk_partition_info_t *info) {
    return info && public_partition_lookup(index, info, NULL);
}

uint32_t bk_device_pci_count(void) {
    return pci_device_count();
}

bool bk_device_pci_info(uint32_t index, bk_pci_info_t *info) {
    const pci_device_t *device;
    if (!info) return false;
    device = pci_device_at(index);
    if (!device) return false;
    kmemset(info, 0, sizeof(*info));
    info->bus = device->bus;
    info->slot = device->slot;
    info->function = device->function;
    info->vendor_id = device->vendor_id;
    info->device_id = device->device_id;
    info->revision_id = device->revision_id;
    info->prog_if = device->prog_if;
    info->subclass = device->subclass;
    info->class_code = device->class_code;
    info->interrupt_line = device->interrupt_line;
    public_copy_text(info->class_name, sizeof(info->class_name),
                     pci_class_name(device->class_code, device->subclass));
    return true;
}

/* Mantener estas exportaciones en un objeto reconstruido junto con la ABI
 * publica. Un build incremental anterior podia conservar public_api.o de v2. */
uint32_t bk_device_driver_count(void) {
    return driver_count();
}

bool bk_device_driver_info(uint32_t index, bk_driver_info_t *info) {
    const bk_loaded_driver_t *driver;
    if (!info) return false;
    driver = driver_at(index);
    if (!driver) return false;
    kmemset(info, 0, sizeof(*info));
    public_copy_text(info->name, sizeof(info->name), driver->name);
    public_copy_text(info->path, sizeof(info->path), driver->path);
    public_copy_text(info->description, sizeof(info->description),
                     driver->description ? driver->description : "");
    return true;
}

bool bk_device_volume_info(bk_volume_info_t *info) {
    fat_fs_t filesystem;
    uint64_t total = 0;
    uint64_t free = 0;
    if (!info) return false;
    kmemset(info, 0, sizeof(*info));
    if (!vfs_get_fs_info(&filesystem)) return false;
    info->mounted = true;
    public_copy_text(info->mount_name, sizeof(info->mount_name),
                     vfs_get_mount_name());
    public_copy_text(info->device_name, sizeof(info->device_name),
                     filesystem.device ? filesystem.device->name : "");
    public_copy_text(info->filesystem, sizeof(info->filesystem),
                     filesystem.fs_name);
    public_copy_text(info->volume_label, sizeof(info->volume_label),
                     filesystem.volume_label);
    info->fat_bits = (uint8_t)filesystem.type;
    info->bytes_per_sector = filesystem.bytes_per_sector;
    info->sectors_per_cluster = filesystem.sectors_per_cluster;
    info->total_sectors = filesystem.total_sectors;
    info->total_clusters = filesystem.total_clusters;
    info->read_only = filesystem.device && filesystem.device->read_only;
    if (vfs_get_space(&total, &free)) {
        info->total_bytes = total;
        info->free_bytes = free;
    }
    return true;
}

bool bk_device_check_volume(bk_volume_check_report_t *report) {
    fat_check_report_t internal;
    bool completed;
    if (!report) return false;
    kmemset(report, 0, sizeof(*report));
    completed = fat_check_current(&internal);
    report->completed = internal.completed;
    report->boot_sector_valid = internal.boot_sector_valid;
    report->fat_copies_match = internal.fat_copies_match;
    report->backup_boot_matches = internal.backup_boot_matches;
    report->files = internal.files;
    report->directories = internal.directories;
    report->allocated_clusters = internal.allocated_clusters;
    report->referenced_clusters = internal.referenced_clusters;
    report->free_clusters = internal.free_clusters;
    report->bad_clusters = internal.bad_clusters;
    report->reserved_clusters = internal.reserved_clusters;
    report->fragmented_files = internal.fragmented_files;
    report->total_fragments = internal.total_fragments;
    report->largest_free_run = internal.largest_free_run;
    report->lost_clusters = internal.lost_clusters;
    report->crosslinked_clusters = internal.crosslinked_clusters;
    report->circular_chains = internal.circular_chains;
    report->invalid_chains = internal.invalid_chains;
    report->size_mismatches = internal.size_mismatches;
    report->directory_errors = internal.directory_errors;
    report->fat_mismatch_sectors = internal.fat_mismatch_sectors;
    report->io_errors = internal.io_errors;
    report->errors = internal.errors;
    report->warnings = internal.warnings;
    return completed;
}

bool bk_device_repair_volume(bk_volume_repair_report_t *repair,
                             bk_volume_check_report_t *after) {
    fat_repair_report_t internal_repair;
    fat_check_report_t internal_after;
    bool completed;
    if (!repair || !after) return false;
    kmemset(repair, 0, sizeof(*repair));
    kmemset(after, 0, sizeof(*after));
    completed = fat_repair_current(&internal_repair, &internal_after);
    repair->attempted = internal_repair.attempted;
    repair->completed = internal_repair.completed;
    repair->read_only = internal_repair.read_only;
    repair->scan_incomplete = internal_repair.scan_incomplete;
    repair->fat_copies_synchronized =
        internal_repair.fat_copies_synchronized;
    repair->backup_boot_repaired = internal_repair.backup_boot_repaired;
    repair->chains_truncated = internal_repair.chains_truncated;
    repair->lost_clusters_freed = internal_repair.lost_clusters_freed;
    repair->unrepaired_crosslinks = internal_repair.unrepaired_crosslinks;
    repair->write_errors = internal_repair.write_errors;
    repair->errors_before = internal_repair.errors_before;
    repair->errors_after = internal_repair.errors_after;
    after->completed = internal_after.completed;
    after->boot_sector_valid = internal_after.boot_sector_valid;
    after->fat_copies_match = internal_after.fat_copies_match;
    after->backup_boot_matches = internal_after.backup_boot_matches;
    after->files = internal_after.files;
    after->directories = internal_after.directories;
    after->allocated_clusters = internal_after.allocated_clusters;
    after->referenced_clusters = internal_after.referenced_clusters;
    after->free_clusters = internal_after.free_clusters;
    after->bad_clusters = internal_after.bad_clusters;
    after->reserved_clusters = internal_after.reserved_clusters;
    after->fragmented_files = internal_after.fragmented_files;
    after->total_fragments = internal_after.total_fragments;
    after->largest_free_run = internal_after.largest_free_run;
    after->lost_clusters = internal_after.lost_clusters;
    after->crosslinked_clusters = internal_after.crosslinked_clusters;
    after->circular_chains = internal_after.circular_chains;
    after->invalid_chains = internal_after.invalid_chains;
    after->size_mismatches = internal_after.size_mismatches;
    after->directory_errors = internal_after.directory_errors;
    after->fat_mismatch_sectors = internal_after.fat_mismatch_sectors;
    after->io_errors = internal_after.io_errors;
    after->errors = internal_after.errors;
    after->warnings = internal_after.warnings;
    return completed;
}

bool bk_device_mount_volume(const char *device_name) {
    return device_name && vfs_mount(device_name);
}
