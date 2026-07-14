#ifndef BLESKERNOS_APPLICATION_API_H
#define BLESKERNOS_APPLICATION_API_H

/*
 * ABI publica para aplicaciones nativas ET_REL de BlesKernOS.
 *
 * Este archivo es deliberadamente independiente del kernel: no incluye
 * drivers, VFS, GUI ni estructuras privadas. Los programas reciben copias de
 * la informacion del sistema y nunca punteros a dispositivos/controladores.
 */
#ifndef TYPES_H
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed int         int32_t;
typedef uint32_t           size_t;
typedef uint8_t            bool;
#define true  1
#define false 0
#define NULL ((void *)0)
#endif

#ifndef UNUSED
#define UNUSED __attribute__((unused))
#endif
#ifndef NORETURN
#define NORETURN __attribute__((noreturn))
#endif

#define BK_APPLICATION_API_VERSION 8U
#define BK_PATH_MAX                260U
#define BK_NAME_MAX                256U
#define BK_DIRECTORY_MAX           128U
#define BK_FILE_READ_ONLY          0x0001U
#define BK_FILE_WRITE_ONLY         0x0002U
#define BK_FILE_READ_WRITE         0x0003U

typedef enum {
    BK_FILE_NODE_NONE = 0,
    BK_FILE_NODE_FILE,
    BK_FILE_NODE_DIRECTORY
} bk_file_node_type_t;

typedef struct {
    char name[BK_NAME_MAX];
    uint32_t size;
    bk_file_node_type_t type;
    uint8_t attributes;
} bk_file_entry_t;

typedef enum {
    BK_PROCESS_UNUSED = 0,
    BK_PROCESS_READY,
    BK_PROCESS_RUNNING,
    BK_PROCESS_SLEEPING,
    BK_PROCESS_ZOMBIE
} bk_process_state_t;

typedef struct {
    uint32_t pid;
    uint32_t process_id;
    char name[24];
    bk_process_state_t state;
    uint32_t cpu_ticks;
    uint32_t memory_bytes;
    bool system;
    bool user;
    bool exit_requested;
} bk_process_info_t;

typedef struct {
    size_t total_bytes;
    size_t used_bytes;
    size_t free_bytes;
    size_t reserved_bytes;
} bk_memory_info_t;

typedef struct {
    struct { uint16_t year; uint8_t month; uint8_t day; } date;
    struct { uint8_t hour; uint8_t minute; uint8_t second; } time;
} bk_datetime_t;

typedef enum {
    BK_BLOCK_NONE = 0,
    BK_BLOCK_ATA,
    BK_BLOCK_FLOPPY,
    BK_BLOCK_ATAPI,
    BK_BLOCK_USB
} bk_block_type_t;

typedef struct {
    char name[16];
    char type_name[16];
    bk_block_type_t type;
    uint32_t sector_count;
    uint16_t sector_size;
    bool read_only;
    bool removable;
} bk_block_info_t;

typedef struct {
    char device_name[16];
    uint8_t table_index;
    bool bootable;
    uint8_t type;
    char type_name[24];
    uint32_t first_sector;
    uint32_t sector_count;
    uint64_t size_bytes;
} bk_partition_info_t;

typedef struct {
    bool mounted;
    char mount_name[16];
    char device_name[16];
    char filesystem[9];
    char volume_label[12];
    uint8_t fat_bits;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint32_t total_sectors;
    uint32_t total_clusters;
    uint64_t total_bytes;
    uint64_t free_bytes;
    bool read_only;
} bk_volume_info_t;

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t revision_id;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t class_code;
    uint8_t interrupt_line;
    char class_name[48];
} bk_pci_info_t;

typedef struct {
    bool completed;
    bool boot_sector_valid;
    bool fat_copies_match;
    bool backup_boot_matches;
    uint32_t files;
    uint32_t directories;
    uint32_t allocated_clusters;
    uint32_t referenced_clusters;
    uint32_t free_clusters;
    uint32_t bad_clusters;
    uint32_t reserved_clusters;
    uint32_t fragmented_files;
    uint32_t total_fragments;
    uint32_t largest_free_run;
    uint32_t lost_clusters;
    uint32_t crosslinked_clusters;
    uint32_t circular_chains;
    uint32_t invalid_chains;
    uint32_t size_mismatches;
    uint32_t directory_errors;
    uint32_t fat_mismatch_sectors;
    uint32_t io_errors;
    uint32_t errors;
    uint32_t warnings;
} bk_volume_check_report_t;

typedef struct {
    bool attempted;
    bool completed;
    bool read_only;
    bool scan_incomplete;
    bool fat_copies_synchronized;
    bool backup_boot_repaired;
    uint32_t chains_truncated;
    uint32_t lost_clusters_freed;
    uint32_t unrepaired_crosslinks;
    uint32_t write_errors;
    uint32_t errors_before;
    uint32_t errors_after;
} bk_volume_repair_report_t;

/* Tipos opacos y geometría de la API GUI para aplicaciones Ring 3. */
typedef struct bk_gui_desktop bk_gui_desktop_t;
typedef struct bk_gui_window bk_gui_window_t;
typedef struct bk_gui_surface bk_gui_surface_t;
typedef struct {
    int x;
    int y;
    int w;
    int h;
} bk_gui_rect_t;
typedef enum {
    BK_GUI_EVENT_NONE = 0,
    BK_GUI_EVENT_MOUSE_MOVE,
    BK_GUI_EVENT_MOUSE_DOWN,
    BK_GUI_EVENT_MOUSE_UP,
    BK_GUI_EVENT_MOUSE_WHEEL,
    BK_GUI_EVENT_KEY
} bk_gui_event_type_t;
typedef struct {
    bk_gui_event_type_t type;
    int x;
    int y;
    int dx;
    int dy;
    uint8_t buttons;
    uint8_t button;
    char key;
    bool shift;
    bool ctrl;
    bool alt;
} bk_gui_event_t;
typedef void (*bk_gui_paint_t)(bk_gui_window_t *window,
                               bk_gui_surface_t *surface, void *context);
typedef bool (*bk_gui_event_handler_t)(bk_gui_window_t *window,
                                       const bk_gui_event_t *event,
                                       void *context);

/* Sistema, consola y runtime minimo. */
uint32_t bk_sys_api_version(void);
uint32_t bk_sys_getpid(void);
uint32_t bk_sys_ticks(void);
uint32_t bk_sys_uptime_ms(void);
void bk_sys_sleep_ms(uint32_t milliseconds);
void bk_sys_reboot(void);
void bk_sys_shutdown(void);
void *bk_sys_alloc(size_t size);
void bk_sys_free(void *pointer);
bool bk_sys_memory_info(bk_memory_info_t *info);
void bk_console_putchar(char character);
void bk_console_write(const char *text);
void bk_console_printf(const char *format, ...);
size_t bk_string_length(const char *text);
int bk_string_compare(const char *left, const char *right);
char *bk_string_copy_n(char *destination, const char *source, size_t capacity);
char *bk_string_concat(char *destination, const char *source);
int bk_memory_compare(const void *left, const void *right, size_t size);

/* Archivos y aplicaciones. */
int bk_file_open(const char *path, uint32_t flags);
bool bk_file_close(int descriptor);
bool bk_file_read_all(const char *path, void **buffer, uint32_t *size);
bool bk_file_write_all(const char *path, const void *buffer, uint32_t size);
bool bk_file_list_dir(const char *path, bk_file_entry_t *entries,
                      uint32_t capacity, uint32_t *count);
bool bk_file_chdir(const char *path);
bool bk_file_mkdir(const char *path);
bool bk_file_remove(const char *path);
bool bk_file_rename(const char *old_path, const char *new_path);
bool bk_app_launch(const char *path, const char *argument);
bool bk_proc_launch_arg_copy(char *buffer, uint32_t capacity);

/* Procesos, reloj y sonido. */
uint32_t bk_proc_count(void);
bool bk_proc_info(uint32_t index, bk_process_info_t *info);
bool bk_proc_request_exit(uint32_t pid);
uint32_t bk_proc_cpu_usage(void);
void bk_proc_exit(void) NORETURN;
bool bk_time_datetime(bk_datetime_t *datetime);
bool bk_sound_tone(uint32_t frequency_hz, uint32_t duration_ms);
const char *bk_sound_pcm_name(void);

/* Hardware y volumenes: siempre copian DTO publicos. */
uint32_t bk_device_block_count(void);
bool bk_device_block_info(uint32_t index, bk_block_info_t *info);
uint32_t bk_device_partition_count(void);
bool bk_device_partition_info(uint32_t index, bk_partition_info_t *info);
bool bk_device_pci_info(uint32_t index, bk_pci_info_t *info);
uint32_t bk_device_pci_count(void);
bool bk_device_volume_info(bk_volume_info_t *info);
bool bk_device_check_volume(bk_volume_check_report_t *report);
bool bk_device_repair_volume(bk_volume_repair_report_t *repair,
                             bk_volume_check_report_t *after);
bool bk_device_mount_volume(const char *device_name);
bool bk_device_format_fat(const char *device_name, const char *volume_label);

/* Ventanas y dibujo: todos los objetos permanecen opacos para la app. */
bk_gui_desktop_t *bk_gui_desktop(void);
bk_gui_window_t *bk_gui_create_window(bk_gui_desktop_t *desktop, int x, int y,
                                      int w, int h, const char *title);
void bk_gui_set_window_content(bk_gui_window_t *window, bk_gui_paint_t paint,
                               void *context);
void bk_gui_set_window_event_handler(bk_gui_window_t *window,
                                     bk_gui_event_handler_t handler,
                                     void *context);
void bk_gui_set_window_min_size(bk_gui_window_t *window, int min_w, int min_h);
void bk_gui_destroy_window(bk_gui_desktop_t *desktop, bk_gui_window_t *window);
bool bk_gui_window_is_open(const bk_gui_window_t *window);
void bk_gui_window_set_owner(bk_gui_window_t *window, uint32_t pid);
void bk_gui_window_invalidate(bk_gui_window_t *window);
bool bk_gui_window_bounds(const bk_gui_window_t *window, bk_gui_rect_t *bounds);
bool bk_gui_window_content_rect(const bk_gui_window_t *window,
                                bk_gui_rect_t *rect);
void bk_gui_surface_clear(bk_gui_surface_t *surface, uint32_t color);
void bk_gui_surface_fill_rect(bk_gui_surface_t *surface, bk_gui_rect_t rect,
                              uint32_t color);
void bk_gui_surface_draw_rect(bk_gui_surface_t *surface, bk_gui_rect_t rect,
                              uint32_t color);
void bk_gui_surface_draw_text(bk_gui_surface_t *surface, int x, int y,
                              const char *text, uint32_t foreground,
                              uint32_t background, bool fill_background);
uint16_t bk_gui_text_width(const char *text);
void bk_gui_request_paint(void);
void bk_proc_bind_window(bk_gui_window_t *window);
bool bk_proc_exit_requested(void);

/* Alias fuente temporales; no exponen ningun tipo privado. */
#ifndef BK_API_NO_LEGACY_ALIASES
typedef bk_file_entry_t vfs_dir_entry_t;
typedef bk_process_info_t bk_proc_info_t;
typedef bk_memory_info_t system_memory_info_t;
typedef bk_datetime_t rtc_datetime_t;
#define VFS_MAX_PATH        BK_PATH_MAX
#define VFS_MAX_DIR_ENTRIES BK_DIRECTORY_MAX
#define VFS_NODE_DIR        BK_FILE_NODE_DIRECTORY
#define kprintf             bk_console_printf
#define bk_runtime_strlen   bk_string_length
#define bk_runtime_strcmp   bk_string_compare
#define bk_runtime_strncpy  bk_string_copy_n
#define bk_runtime_strcat   bk_string_concat
#define bk_runtime_memcmp   bk_memory_compare
#endif

#endif
