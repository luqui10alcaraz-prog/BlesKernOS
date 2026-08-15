#include "include/elf_loader.h"
#include "include/pe_loader.h"
#include "include/memory.h"
#include "include/vfs.h"
#include "include/about_dialog.h"
#include "include/startup_sound.h"
#include "include/task.h"
#include "include/kernel_domains.h"
#include "include/klock.h"
#include "include/gdt.h"
#include "include/pit.h"
#include "include/compat_mode.h"
#include "include/keyboard.h"
#include "include/block.h"
#include "include/pci.h"
#include "include/iso9660.h"
#include "include/shell.h"
#include "include/sound.h"
#include "include/vga.h"
#include "include/datetime_prefs.h"
#include "../gui/gui.h"
#include "../gui/image.h"
#include "../programs/programs.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "strings.h"
#include "ctype.h"
#include "math.h"

#ifndef ELF_LOAD_TIMING
#define ELF_LOAD_TIMING 0
#endif
#include "errno.h"
#include "sys/stat.h"
#include "include/rtc.h"
#include "include/api.h"
#include "include/ata.h"
#include "include/driver.h"
#include "include/usb_core.h"
#include "include/lpt.h"
#include "include/gfx_driver.h"
#include "include/gfx3d_driver.h"
#include "include/rage128_engine.h"
#include "include/svga_transport.h"
#include "include/pic.h"
#include "include/mouse.h"
#include "include/vesa.h"
#include "include/syscall.h"
#include "include/network.h"

#include "win32/win32.h"
/* api_compat conserva nombres historicos para codigo interno. El cargador debe
 * exportar los simbolos reales de la ABI publica, no expandirlos a drivers. */
#undef bk_proc_cpu_usage
#undef bk_device_block_count
#undef bk_device_pci_count
#undef bk_device_driver_count
extern void bk_console_printf(const char *format, ...);
extern size_t bk_string_length(const char *text);
extern int bk_string_compare(const char *left, const char *right);
extern char *bk_string_copy_n(char *destination, const char *source,
                              size_t capacity);
extern char *bk_string_concat(char *destination, const char *source);
extern int bk_memory_compare(const void *left, const void *right, size_t size);
extern bool bk_proc_launch_arg_copy(char *buffer, uint32_t capacity);
extern uint32_t bk_proc_cpu_usage(void);
extern uint32_t bk_device_block_count(void);
extern bool bk_device_block_info(uint32_t index, bk_block_info_t *info);
extern uint32_t bk_device_pci_count(void);
extern bool bk_device_pci_info(uint32_t index, bk_pci_info_t *info);
extern uint32_t bk_device_driver_count(void);
extern bool bk_device_driver_info(uint32_t index, bk_driver_info_t *info);
extern bool bk_device_volume_info(void *info);
extern bool bk_device_check_volume(void *report);
extern bool bk_device_repair_volume(void *repair, void *after);
extern uint32_t bk_device_partition_count(void);
extern bool bk_device_partition_info(uint32_t index, void *info);
extern bool bk_device_mount_volume(const char *device_name);

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
#define STB_WEAK 2
#define ELF32_ST_BIND(INFO) ((INFO) >> 4)
#define R_386_NONE 0
#define R_386_32 1
#define R_386_PC32 2
#define ELF_USER_THUNK_MAX 1024U
#define ELF_USER_THUNK_SIZE 48U
#define ELF_USER_CALL_WORDS 16U

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
typedef struct {
    uint32_t process_id;
    void *image;
} elf_process_image_t;

static elf_process_image_t g_process_images[TASK_MAX];
/* User-callable thunks cannot live in kernel BSS now that Phase 1 marks
 * kernel pages supervisor-only. Allocate them from the shared user heap and
 * publish each pointer only after the code bytes are complete. */
static uint8_t *g_user_thunks[ELF_USER_THUNK_MAX];
static uint32_t g_user_targets[ELF_USER_THUNK_MAX];
static kspinlock_t g_user_thunk_lock = KSPINLOCK_INITIALIZER;
static uint32_t *g_user_errno_slot;
static uint32_t *g_user_stdout_slot;
static uint32_t *g_user_stderr_slot;
/* Stable metadata for user-callable API thunks.
 *
 * The ELF symbol name passed to elf_user_api_thunk() points into the image
 * currently being relocated.  That image is released when its process exits,
 * while the thunk/token cache is global and survives for the lifetime of the
 * kernel.  Keeping the original pointer therefore turns every later API call
 * into a use-after-free inside kernel_domain_mask_for_api().
 *
 * Classify while the loader string is valid and retain only a bounded label
 * for diagnostics.  Both arrays live in supervisor-only BSS, so Ring 3 cannot
 * rewrite the lock-domain decision. */
#define ELF_USER_API_LABEL_MAX 32U
static uint32_t g_user_api_domains[ELF_USER_THUNK_MAX];
static char g_user_api_labels[ELF_USER_THUNK_MAX][ELF_USER_API_LABEL_MAX];
static const char *g_last_user_api_name = "(ninguna)";
static uint32_t g_user_thunk_count;

/* BLES_WINE_DIALOG_RING3_FIX_20260723
 * DialogBox* needs a modal loop that actually lives in Ring 3. Static kernel
 * text is below HEAP_START and cannot be a validated return address for a
 * nested SYS_API_CALL, so each shim is allocated from the executable shared
 * heap. */
#define ELF_DIALOG_THUNK_COUNT 4U
#define ELF_DIALOG_THUNK_SIZE 128U
static uint8_t *g_dialog_thunks[ELF_DIALOG_THUNK_COUNT];

static void dialog_emit8(uint8_t *code, uint32_t *position, uint8_t value) {
    code[(*position)++] = value;
}
static void dialog_emit32(uint8_t *code, uint32_t *position, uint32_t value) {
    *(uint32_t *)(void *)(code + *position) = value;
    *position += 4U;
}
static void dialog_patch_rel32(uint8_t *code, uint32_t displacement,
                               uint32_t target) {
    *(int32_t *)(void *)(code + displacement) =
        (int32_t)target - (int32_t)(displacement + 4U);
}

/* BLES_WINE_SYNC_WINDOW_FIX_20260723
 * Same-task SendMessage cannot be represented by the asynchronous WndProc
 * upcall queue: it must call the PE procedure immediately and return LRESULT.
 * This executable user-heap thunk asks USER32 for a call plan, executes the
 * WndProc at CPL3, then reports completion back to the kernel. */
#define ELF_SENDMSG_THUNK_COUNT 2U
#define ELF_SENDMSG_THUNK_SIZE 128U
static uint8_t *g_sendmsg_thunks[ELF_SENDMSG_THUNK_COUNT];

static uint32_t elf_user_send_message_thunk(const char *name) {
    uint32_t index, prepare_target, prepare_thunk, complete_thunk;
    uint8_t *code;
    uint32_t p = 0U, no_invoke_disp, done_disp, no_invoke_pos, done_pos;

    if (!name) return 0U;
    if (kstrcmp(name, "SendMessageA") == 0) {
        index = 0U;
        prepare_target = (uint32_t)(uintptr_t)
            &win32_user_send_message_prepare_a;
    } else if (kstrcmp(name, "SendMessageW") == 0) {
        index = 1U;
        prepare_target = (uint32_t)(uintptr_t)
            &win32_user_send_message_prepare_w;
    } else return 0U;

    if (g_sendmsg_thunks[index])
        return (uint32_t)(uintptr_t)g_sendmsg_thunks[index];

    prepare_thunk = elf_user_api_thunk(
        index ? "__BlesSendMessagePrepareW" : "__BlesSendMessagePrepareA",
        prepare_target);
    complete_thunk = elf_user_api_thunk("__BlesSendMessageComplete",
        (uint32_t)(uintptr_t)&win32_user_send_message_complete);
    if (!prepare_thunk || !complete_thunk) return 0U;

    code = (uint8_t *)kmalloc(ELF_SENDMSG_THUNK_SIZE);
    if (!code) return 0U;
    (void)mm_set_allocation_owner(code, 0U);
    kmemset(code, 0x90, ELF_SENDMSG_THUNK_SIZE);

    /* stdcall SendMessage(hwnd,msg,wp,lp), 32-byte plan at [ebp-36]. */
    dialog_emit8(code, &p, 0x55);                         /* push ebp */
    dialog_emit8(code, &p, 0x89); dialog_emit8(code, &p, 0xE5); /* mov ebp,esp */
    dialog_emit8(code, &p, 0x53);                         /* push ebx */
    dialog_emit8(code, &p, 0x83); dialog_emit8(code, &p, 0xEC);
    dialog_emit8(code, &p, 0x20);                         /* sub esp,32 */
    dialog_emit8(code, &p, 0x8D); dialog_emit8(code, &p, 0x5D);
    dialog_emit8(code, &p, 0xDC);                         /* lea ebx,[ebp-36] */

    dialog_emit8(code, &p, 0x53);                         /* push plan */
    for (int offset = 20; offset >= 8; offset -= 4) {
        dialog_emit8(code, &p, 0xFF); dialog_emit8(code, &p, 0x75);
        dialog_emit8(code, &p, (uint8_t)offset);           /* push [ebp+off] */
    }
    dialog_emit8(code, &p, 0xB8); dialog_emit32(code, &p, prepare_thunk);
    dialog_emit8(code, &p, 0xFF); dialog_emit8(code, &p, 0xD0); /* call eax */
    dialog_emit8(code, &p, 0x85); dialog_emit8(code, &p, 0xC0); /* test eax,eax */
    dialog_emit8(code, &p, 0x0F); dialog_emit8(code, &p, 0x84);
    no_invoke_disp = p; dialog_emit32(code, &p, 0U);       /* jz no_invoke */

    /* Direct same-thread WndProc(hwnd,msg,wp,lp), stdcall ret 16. */
    for (int offset = 20; offset >= 8; offset -= 4) {
        dialog_emit8(code, &p, 0xFF); dialog_emit8(code, &p, 0x73);
        dialog_emit8(code, &p, (uint8_t)offset);           /* push [ebx+off] */
    }
    dialog_emit8(code, &p, 0x8B); dialog_emit8(code, &p, 0x43);
    dialog_emit8(code, &p, 0x04);                         /* mov eax,[ebx+4] */
    dialog_emit8(code, &p, 0xFF); dialog_emit8(code, &p, 0xD0); /* call eax */

    dialog_emit8(code, &p, 0x50);                         /* push result */
    dialog_emit8(code, &p, 0x53);                         /* push plan */
    dialog_emit8(code, &p, 0xB8); dialog_emit32(code, &p, complete_thunk);
    dialog_emit8(code, &p, 0xFF); dialog_emit8(code, &p, 0xD0); /* call eax */
    dialog_emit8(code, &p, 0xE9); done_disp = p;
    dialog_emit32(code, &p, 0U);                          /* jmp done */

    no_invoke_pos = p;
    dialog_emit8(code, &p, 0x8B); dialog_emit8(code, &p, 0x43);
    dialog_emit8(code, &p, 0x18);                         /* mov eax,[ebx+24] */

    done_pos = p;
    dialog_emit8(code, &p, 0x8D); dialog_emit8(code, &p, 0x65);
    dialog_emit8(code, &p, 0xFC);                         /* lea esp,[ebp-4] */
    dialog_emit8(code, &p, 0x5B);                         /* pop ebx */
    dialog_emit8(code, &p, 0x5D);                         /* pop ebp */
    dialog_emit8(code, &p, 0xC2); dialog_emit8(code, &p, 0x10);
    dialog_emit8(code, &p, 0x00);                         /* ret 16 */

    if (p > ELF_SENDMSG_THUNK_SIZE) { kfree(code); return 0U; }
    dialog_patch_rel32(code, no_invoke_disp, no_invoke_pos);
    dialog_patch_rel32(code, done_disp, done_pos);
    g_sendmsg_thunks[index] = code;
    return (uint32_t)(uintptr_t)code;
}

static uint32_t elf_user_dialog_thunk(const char *name) {
    uint32_t index;
    uint32_t begin_target;
    const char *begin_name;
    uint32_t begin_thunk, step_thunk;
    uint8_t *code;
    uint32_t p = 0U, fail_disp, loop_pos, loop_disp, invoke_disp;
    uint32_t invoke_pos, invoke_loop_disp, done_disp, done_pos;

    if (!name) return 0U;
    if (kstrcmp(name, "DialogBoxParamA") == 0) {
        index = 0U; begin_target = (uint32_t)(uintptr_t)
            &win32_user_dialog_begin_param_a;
        begin_name = "__BlesDialogBeginParamA";
    } else if (kstrcmp(name, "DialogBoxParamW") == 0) {
        index = 1U; begin_target = (uint32_t)(uintptr_t)
            &win32_user_dialog_begin_param_w;
        begin_name = "__BlesDialogBeginParamW";
    } else if (kstrcmp(name, "DialogBoxIndirectParamA") == 0) {
        index = 2U; begin_target = (uint32_t)(uintptr_t)
            &win32_user_dialog_begin_indirect_a;
        begin_name = "__BlesDialogBeginIndirectA";
    } else if (kstrcmp(name, "DialogBoxIndirectParamW") == 0) {
        index = 3U; begin_target = (uint32_t)(uintptr_t)
            &win32_user_dialog_begin_indirect_w;
        begin_name = "__BlesDialogBeginIndirectW";
    } else return 0U;

    if (g_dialog_thunks[index])
        return (uint32_t)(uintptr_t)g_dialog_thunks[index];

    begin_thunk = elf_user_api_thunk(begin_name, begin_target);
    step_thunk = elf_user_api_thunk("__BlesDialogModalStep",
        (uint32_t)(uintptr_t)&win32_user_dialog_modal_step);
    if (!begin_thunk || !step_thunk) return 0U;

    code = (uint8_t *)kmalloc(ELF_DIALOG_THUNK_SIZE);
    if (!code) return 0U;
    (void)mm_set_allocation_owner(code, 0U);
    kmemset(code, 0x90, ELF_DIALOG_THUNK_SIZE);

    /* stdcall DialogBox*(a1..a5): create, then execute one kernel message
     * iteration per syscall return so queued WndProcs can run at CPL3. */
    dialog_emit8(code, &p, 0x55);                         /* push ebp */
    dialog_emit8(code, &p, 0x89); dialog_emit8(code, &p, 0xE5); /* mov ebp,esp */
    dialog_emit8(code, &p, 0x53);                         /* push ebx */
    dialog_emit8(code, &p, 0x83); dialog_emit8(code, &p, 0xEC);
    dialog_emit8(code, &p, 0x1C);                         /* sub esp,28 */
    dialog_emit8(code, &p, 0xC7); dialog_emit8(code, &p, 0x45);
    dialog_emit8(code, &p, 0xE0); dialog_emit32(code, &p, 0xFFFFFFFFU);
                                                               /* result=-1 */
    for (int offset = 24; offset >= 8; offset -= 4) {
        dialog_emit8(code, &p, 0xFF); dialog_emit8(code, &p, 0x75);
        dialog_emit8(code, &p, (uint8_t)offset);           /* push [ebp+off] */
    }
    dialog_emit8(code, &p, 0xB8); dialog_emit32(code, &p, begin_thunk);
    dialog_emit8(code, &p, 0xFF); dialog_emit8(code, &p, 0xD0); /* call eax */
    dialog_emit8(code, &p, 0x85); dialog_emit8(code, &p, 0xC0); /* test eax,eax */
    dialog_emit8(code, &p, 0x0F); dialog_emit8(code, &p, 0x84);
    fail_disp = p; dialog_emit32(code, &p, 0U);             /* jz fail */
    dialog_emit8(code, &p, 0x89); dialog_emit8(code, &p, 0xC3); /* mov ebx,eax */

    loop_pos = p;
    dialog_emit8(code, &p, 0x8D); dialog_emit8(code, &p, 0x45);
    dialog_emit8(code, &p, 0xE0);                         /* lea eax,[ebp-32] */
    dialog_emit8(code, &p, 0x50);                          /* push eax */
    dialog_emit8(code, &p, 0x53);                          /* push ebx */
    dialog_emit8(code, &p, 0xB8); dialog_emit32(code, &p, step_thunk);
    dialog_emit8(code, &p, 0xFF); dialog_emit8(code, &p, 0xD0); /* call eax */
    dialog_emit8(code, &p, 0x83); dialog_emit8(code, &p, 0xF8);
    dialog_emit8(code, &p, 0x02);                          /* cmp eax,2 */
    dialog_emit8(code, &p, 0x0F); dialog_emit8(code, &p, 0x84);
    invoke_disp = p; dialog_emit32(code, &p, 0U);          /* je invoke */
    dialog_emit8(code, &p, 0x85); dialog_emit8(code, &p, 0xC0);
    dialog_emit8(code, &p, 0x0F); dialog_emit8(code, &p, 0x84);
    loop_disp = p; dialog_emit32(code, &p, 0U);             /* jz loop */
    dialog_emit8(code, &p, 0x8B); dialog_emit8(code, &p, 0x45);
    dialog_emit8(code, &p, 0xE0);                         /* mov eax,[ebp-32] */
    dialog_emit8(code, &p, 0xE9); done_disp = p;
    dialog_emit32(code, &p, 0U);                           /* jmp done */

    uint32_t fail_pos = p;
    dialog_emit8(code, &p, 0xB8); dialog_emit32(code, &p, 0xFFFFFFFFU);
    dialog_emit8(code, &p, 0xE9);
    uint32_t fail_done_disp = p; dialog_emit32(code, &p, 0U);

    invoke_pos = p;
    /* Direct nested WndProc(hwnd,message,wparam,lparam), stdcall ret 16. */
    dialog_emit8(code, &p, 0xFF); dialog_emit8(code, &p, 0x75);
    dialog_emit8(code, &p, 0xF8);                         /* push [ebp-8] */
    dialog_emit8(code, &p, 0xFF); dialog_emit8(code, &p, 0x75);
    dialog_emit8(code, &p, 0xF4);                         /* push [ebp-12] */
    dialog_emit8(code, &p, 0xFF); dialog_emit8(code, &p, 0x75);
    dialog_emit8(code, &p, 0xF0);                         /* push [ebp-16] */
    dialog_emit8(code, &p, 0xFF); dialog_emit8(code, &p, 0x75);
    dialog_emit8(code, &p, 0xEC);                         /* push [ebp-20] */
    dialog_emit8(code, &p, 0xFF); dialog_emit8(code, &p, 0x55);
    dialog_emit8(code, &p, 0xE8);                         /* call [ebp-24] */
    dialog_emit8(code, &p, 0xE9);
    invoke_loop_disp = p; dialog_emit32(code, &p, 0U);    /* jmp loop */

    done_pos = p;
    dialog_emit8(code, &p, 0x8D); dialog_emit8(code, &p, 0x65);
    dialog_emit8(code, &p, 0xFC);                          /* lea esp,[ebp-4] */
    dialog_emit8(code, &p, 0x5B);                          /* pop ebx */
    dialog_emit8(code, &p, 0x5D);                          /* pop ebp */
    dialog_emit8(code, &p, 0xC2); dialog_emit8(code, &p, 0x14);
    dialog_emit8(code, &p, 0x00);                          /* ret 20 */

    if (p > ELF_DIALOG_THUNK_SIZE) { kfree(code); return 0U; }
    dialog_patch_rel32(code, fail_disp, fail_pos);
    dialog_patch_rel32(code, loop_disp, loop_pos);
    dialog_patch_rel32(code, invoke_disp, invoke_pos);
    dialog_patch_rel32(code, invoke_loop_disp, loop_pos);
    dialog_patch_rel32(code, done_disp, done_pos);
    dialog_patch_rel32(code, fail_done_disp, done_pos);
    g_dialog_thunks[index] = code;
    return (uint32_t)(uintptr_t)code;
}

bool elf_preview_create(const char *path, gui_desktop_t *desktop,
                        void **image_out);
void elf_preview_destroy(void *image);

extern uint64_t __udivdi3(uint64_t a, uint64_t b);
extern uint64_t __umoddi3(uint64_t a, uint64_t b);
extern int64_t __divdi3(int64_t a, int64_t b);
extern int64_t __moddi3(int64_t a, int64_t b);

static bool elf_range_ok(uint32_t offset, uint32_t length, uint32_t total) {
    return offset <= total && length <= total - offset;
}

static uint32_t elf_align(uint32_t value, uint32_t alignment) {
    if (alignment <= 1) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

extern uint32_t win32_user32_resolve(const char *name);
extern uint32_t win32_kernel32_resolve(const char *name);

#define EXPORT(symbol) \
    if (kstrcmp(name, #symbol) == 0) return (uint32_t)(uintptr_t)&symbol

static uint32_t elf_kernel_symbol(const char *name) {
    /* GCC / runtime */
    EXPORT(__udivdi3);
    EXPORT(__umoddi3);
    EXPORT(__divdi3);
    EXPORT(__moddi3);

    /* libc / stdio / stdlib / string */
    EXPORT(abs);
    EXPORT(atof);
    EXPORT(atoi);
    EXPORT(calloc);
    EXPORT(errno);
    EXPORT(exit);
    EXPORT(fabs);
    EXPORT(atan);
    EXPORT(cos);
    EXPORT(sin);
    EXPORT(tan);
    EXPORT(sqrt);
    EXPORT(floor);
    EXPORT(pow);
    EXPORT(fclose);
    EXPORT(feof);
    EXPORT(ferror);
    EXPORT(fflush);
    EXPORT(fopen);
    EXPORT(fprintf);
    EXPORT(fread);
    EXPORT(free);
    EXPORT(fseek);
    EXPORT(ftell);
    EXPORT(fwrite);
    EXPORT(malloc);
    EXPORT(memcmp);
    EXPORT(memcpy);
    EXPORT(memmove);
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
    EXPORT(stdin);
    EXPORT(stdout);
    EXPORT(strcasecmp);
    EXPORT(strcat);
    EXPORT(strchr);
    EXPORT(strcmp);
    EXPORT(strcpy);
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

    /* BlesKernOS public API */
    EXPORT(bk_sys_api_version);
    EXPORT(bk_sys_capabilities);
    EXPORT(bk_sys_log);
    EXPORT(bk_console_putchar);
    EXPORT(bk_console_write);
    EXPORT(bk_sys_getpid);
    EXPORT(bk_sys_yield);
    EXPORT(bk_sys_sleep_ticks);
    EXPORT(bk_sys_sleep_ms);
    EXPORT(bk_sys_ticks);
    EXPORT(bk_sys_tick_frequency);
    EXPORT(bk_sys_uptime_ms);
    EXPORT(bk_sys_reboot);
    EXPORT(bk_sys_shutdown);
    EXPORT(bk_sys_alloc);
    EXPORT(bk_sys_alloc_zero);
    EXPORT(bk_sys_realloc);
    EXPORT(bk_sys_free);
    EXPORT(bk_sys_memory_info);
    EXPORT(bk_console_printf);
    EXPORT(bk_string_length);
    EXPORT(bk_string_compare);
    EXPORT(bk_string_copy_n);
    EXPORT(bk_string_concat);
    EXPORT(bk_memory_compare);
    EXPORT(bk_proc_launch_arg_copy);
    EXPORT(bk_proc_cpu_usage);
    EXPORT(bk_proc_cpu_count);
    EXPORT(bk_proc_cpu_usage_core);
    EXPORT(bk_proc_current_cpu);
    EXPORT(bk_proc_set_affinity);
    EXPORT(bk_proc_get_affinity);
    EXPORT(bk_proc_runqueue_depth);
    EXPORT(bk_proc_scheduler_steals);
    EXPORT(bk_proc_scheduler_migrations);
    EXPORT(bk_proc_scheduler_ipis);
    EXPORT(bk_perf_enabled);
    EXPORT(bk_perf_set_enabled);
    EXPORT(bk_perf_reset);
    EXPORT(bk_perf_snapshot);
    EXPORT(bk_perf_benchmark);
    EXPORT(bk_device_block_count);
    EXPORT(bk_device_block_info);
    EXPORT(bk_device_pci_count);
    EXPORT(bk_device_pci_info);
    EXPORT(bk_device_driver_count);
    EXPORT(bk_device_driver_info);
    EXPORT(bk_device_volume_info);
    EXPORT(bk_device_check_volume);
    EXPORT(bk_device_repair_volume);
    EXPORT(bk_device_partition_count);
    EXPORT(bk_device_partition_info);
    EXPORT(bk_device_mount_volume);

    EXPORT(bk_net_get_info);
    EXPORT(bk_net_dhcp);
    EXPORT(bk_net_ping);
    EXPORT(bk_net_resolve);
    EXPORT(bk_net_socket_open);
    EXPORT(bk_net_socket_connect);
    EXPORT(bk_net_socket_bind);
    EXPORT(bk_net_socket_listen);
    EXPORT(bk_net_socket_accept);
    EXPORT(bk_net_socket_send);
    EXPORT(bk_net_socket_receive);
    EXPORT(bk_net_socket_sendto);
    EXPORT(bk_net_socket_receivefrom);
    EXPORT(bk_net_socket_readable);
    EXPORT(bk_net_socket_close);
    EXPORT(bk_net_http_get);
    EXPORT(bk_net_http_exchange);
    EXPORT(bk_net_tls_last_error);
    EXPORT(bk_clipboard_set_text);
    EXPORT(bk_clipboard_get_text);
    EXPORT(bk_clipboard_clear);
    EXPORT(bk_clipboard_generation);
    EXPORT(bk_data_inflate_zlib);
    EXPORT(bk_data_inflate_gzip);

    EXPORT(bk_file_open);
    EXPORT(bk_file_read);
    EXPORT(bk_file_write);
    EXPORT(bk_file_close);
    EXPORT(bk_file_read_all);
    EXPORT(bk_file_write_all);
    EXPORT(bk_file_list_dir);
    EXPORT(bk_file_stat);
    EXPORT(bk_file_chdir);
    EXPORT(bk_file_getcwd);
    EXPORT(bk_file_mkdir);
    EXPORT(bk_file_remove);
    EXPORT(bk_file_rename);
    EXPORT(bk_setup_extract_package);
    EXPORT(bk_setup_get_progress);
    EXPORT(bk_file_space);
    EXPORT(bk_lang_get);
    EXPORT(bk_lang_translate);
    EXPORT(bk_lang_current);
    EXPORT(bk_lang_generation);
    EXPORT(bk_lang_count);
    EXPORT(bk_lang_info);
    EXPORT(bk_lang_set);
    EXPORT(bk_device_format_fat);
    EXPORT(bk_print_lpt_count);
    EXPORT(bk_print_lpt_info);
    EXPORT(bk_print_port_info);
    EXPORT(bk_print_lpt_write);
    EXPORT(bk_print_spooler_set_ready);
    EXPORT(bk_print_spooler_is_ready);
    EXPORT(bk_file_dialog_open);

    EXPORT(bk_gui_desktop);
    EXPORT(bk_gui_request_paint);
    EXPORT(bk_gui_create_window);
    EXPORT(bk_gui_alert);
    EXPORT(bk_gui_error);
    EXPORT(bk_gui_network_error);
    EXPORT(bk_gui_close_window);
    EXPORT(bk_gui_focus_window);
    EXPORT(bk_gui_set_window_content);
    EXPORT(bk_gui_set_window_event_handler);
    EXPORT(bk_gui_set_window_min_size);
    EXPORT(bk_gui_add_menu);
    EXPORT(bk_gui_add_menu_item);
    EXPORT(bk_gui_destroy_window);
    EXPORT(bk_gui_window_is_open);
    EXPORT(bk_gui_window_set_owner);
    EXPORT(bk_gui_window_invalidate);
    EXPORT(bk_gui_window_set_gpu_viewport);
    EXPORT(bk_gui_window_clear_gpu_viewport);
    EXPORT(bk_gui_gpu_viewport_supported);
    EXPORT(bk_gui_window_set_text_context);
    EXPORT(bk_gui_window_clear_text_context);
    EXPORT(bk_gui_window_begin_immediate_paint);
    EXPORT(bk_gui_window_end_immediate_paint);
    EXPORT(bk_gui_window_bounds);
    EXPORT(bk_gui_window_content_rect);
    EXPORT(bk_gui_surface_clear);
    EXPORT(bk_gui_surface_putpixel);
    EXPORT(bk_gui_surface_fill_rect);
    EXPORT(bk_gui_surface_draw_rect);
    EXPORT(bk_gui_surface_draw_line);
    EXPORT(bk_gui_surface_draw_polyline);
    EXPORT(bk_gui_surface_draw_text);
    EXPORT(bk_gui_text_width_px);
    EXPORT(bk_gui_surface_draw_text_px);
    EXPORT(bk_gui_image_decode_bmp);
    EXPORT(bk_gui_image_decode_png);
    EXPORT(bk_gui_image_decode_gif);
    EXPORT(bk_gui_image_decode_jpeg);
    EXPORT(bk_gui_image_decode_svg);
    EXPORT(bk_gui_image_free);
    EXPORT(bk_gui_cursor_set_resource);
    EXPORT(bk_gui_cursor_reset);
    EXPORT(bk_gui_cursor_set_style);
    EXPORT(bk_graphics_icon_load);
    EXPORT(bk_graphics_icon_count);
    EXPORT(bk_graphics_icon_name);
    EXPORT(bk_gui_surface_draw_image);
    EXPORT(bk_gui_surface_draw_progress);
    EXPORT(bk_gui_text_width);
    EXPORT(bk_gui_create_button);
    EXPORT(bk_gui_create_textbox);
    EXPORT(bk_gui_widget_id);
    EXPORT(bk_gui_widget_set_bounds);
    EXPORT(bk_gui_widget_set_text);
    EXPORT(bk_gui_widget_set_icon);
    EXPORT(bk_gui_widget_get_text);
    EXPORT(bk_gui_widget_set_enabled);
    EXPORT(bk_gui_widget_set_visible);
    EXPORT(bk_gui_widget_set_focus);
    EXPORT(bk_gui_widget_is_focused);
    EXPORT(bk_gui_scrollbar_init_vertical);
    EXPORT(bk_gui_scrollbar_paint_vertical);
    EXPORT(bk_gui_scrollbar_handle_event_vertical);

    EXPORT(bk_gfx_info);
    EXPORT(bk_gfx_set_mode);
    EXPORT(bk_gfx_clear);
    EXPORT(bk_gfx_putpixel);
    EXPORT(bk_gfx_getpixel);
    EXPORT(bk_gfx_fill_rect);
    EXPORT(bk_gfx_present_rect);
    EXPORT(bk_gfx_flush);
    EXPORT(bk_gfx_copy_rect);
    EXPORT(bk_gfx_draw_line);
    EXPORT(bk_gfx_draw_text);
    EXPORT(bk_input_mouse);
    EXPORT(bk_input_key_event);
    EXPORT(bk_input_key_modifiers);
    EXPORT(bk_input_mouse_set_position);
    EXPORT(bk_input_mouse_set_sensitivity);
    EXPORT(bk_input_mouse_get_sensitivity);

    EXPORT(bk_sound_has_sb16);
    EXPORT(bk_sound_pcm_available);
    EXPORT(bk_sound_pcm_busy);
    EXPORT(bk_sound_pcm_name);
    EXPORT(bk_sound_play_pcm_u8);
    EXPORT(bk_sound_tone);
    EXPORT(bk_sound_stop);
    EXPORT(bk_time_datetime);
    EXPORT(bk_datetime_runtime_preferences_get);
    EXPORT(bk_datetime_runtime_preferences_set);
    EXPORT(bk_proc_count);
    EXPORT(bk_proc_get);
    EXPORT(bk_proc_info);
    EXPORT(bk_proc_snapshot);
    EXPORT(bk_proc_request_exit);
    EXPORT(bk_proc_exit_requested);
    EXPORT(bk_proc_set_memory_hint);
    EXPORT(bk_proc_bind_window);
    EXPORT(bk_proc_launch_arg);
    EXPORT(bk_proc_spawn_thread);
    EXPORT(bk_proc_exit);
    EXPORT(bk_app_launch);
    EXPORT(bk_shell_take_exit_request);

    /* GUI / Desktop */
    EXPORT(gui_change_resolution);
    EXPORT(gui_display_color_depth);
    EXPORT(gui_desktop_create_window);
    EXPORT(gui_desktop_focus_window);
    EXPORT(gui_desktop_invalidate_all);
    EXPORT(gui_desktop_invalidate_rect);
    EXPORT(gui_desktop_raise_window);
    EXPORT(gui_desktop_remove_window);
    EXPORT(gui_desktop_register_program);
    EXPORT(gui_desktop_set_cursor_trail);
    EXPORT(gui_desktop_set_drag_outline);
    EXPORT(gui_desktop_unregister_program);
    EXPORT(gui_desktop_cursor_trail_enabled);
    EXPORT(gui_desktop_drag_outline_enabled);
    EXPORT(gui_get_last_input_tick);

    /* GUI / Drawing */
    EXPORT(gui_color_blend);
    EXPORT(gui_gfx_clear);
    EXPORT(gui_gfx_draw_line);
    EXPORT(gui_gfx_fill_rect);
    EXPORT(gui_gfx_fill_rounded_rect);
    EXPORT(gui_gfx_draw_rect);
    EXPORT(gui_gfx_putpixel);
    EXPORT(gui_gfx_point_visible);
    EXPORT(gui_gfx_invalidate_front);
    EXPORT(gui_font_draw_char);
    EXPORT(gui_font_draw_string);
    EXPORT(gui_font_draw_string_clipped);
    EXPORT(gui_font_draw_string_scaled);
    EXPORT(gui_font_text_width);
    EXPORT(gui_gif_animation_free);
    EXPORT(gui_gif_load);
    EXPORT(gui_gif_load_animation);
    EXPORT(gui_gif_load_animation_limited);
    EXPORT(gui_image_free);
    EXPORT(gui_request_paint);
    EXPORT(gui_get_desktop);

    /* GUI / Window */
    EXPORT(gui_window_add_menu);
    EXPORT(gui_window_add_menu_item);
    EXPORT(gui_window_contains);
    EXPORT(gui_window_content_rect);
    EXPORT(gui_window_content_rect_inset);
    EXPORT(gui_window_content_top);
    EXPORT(gui_window_dispatch_event);
    EXPORT(gui_window_destroy);
    EXPORT(gui_window_handle_menu_event);
    EXPORT(gui_window_context_clear);
    EXPORT(gui_window_context_add_item);
    EXPORT(gui_window_context_open);
    EXPORT(gui_window_context_close);
    EXPORT(gui_context_menu_clear);
    EXPORT(gui_context_menu_add_item);
    EXPORT(gui_context_menu_open);
    EXPORT(gui_context_menu_close);
    EXPORT(gui_context_menu_paint);
    EXPORT(gui_context_menu_handle_event);
    EXPORT(gui_window_set_content);
    EXPORT(gui_window_set_event_handler);
    EXPORT(gui_window_set_min_size);
    EXPORT(gui_window_set_borderless);
    EXPORT(gui_window_close);
    EXPORT(gui_window_minimize);
    EXPORT(gui_window_restore);
    EXPORT(gui_window_titlebar_button_at);
    EXPORT(gui_window_titlebar_contains);

    /* GUI / Widgets */
    EXPORT(gui_widget_create);
    EXPORT(gui_widget_create_button);
    EXPORT(gui_widget_create_selectable_button);
    EXPORT(gui_widget_create_listbox);
    EXPORT(gui_widget_create_dropdown);
    EXPORT(gui_widget_handle_event);
    EXPORT(gui_widget_paint);
    EXPORT(gui_widget_screen_bounds);
    EXPORT(gui_widget_set_style);
    EXPORT(gui_widget_set_selected);
    EXPORT(gui_widget_set_enabled);
    EXPORT(gui_widget_dropdown_clear);
    EXPORT(gui_widget_dropdown_add_item);
    EXPORT(gui_widget_dropdown_get_selected);
    EXPORT(gui_widget_dropdown_set_selected);
    EXPORT(gui_widget_dropdown_set_selected_by_value);
    EXPORT(gui_widget_dropdown_get_selected_label);
    EXPORT(gui_widget_dropdown_get_selected_value);
    EXPORT(gui_widget_dropdown_get_item_label);
    EXPORT(gui_widget_dropdown_get_item_value);

    /* GUI / Events / Geometry */
    EXPORT(gui_event_queue_pop);
    EXPORT(gui_event_queue_push);
    EXPORT(gui_event_queue_reset);
    EXPORT(gui_rect_contains);
    EXPORT(gui_rect_intersect);
    EXPORT(gui_scrollbar_handle_click_vertical);
    EXPORT(gui_scrollbar_handle_event_vertical);
    EXPORT(gui_scrollbar_init_vertical);
    EXPORT(gui_scrollbar_paint_vertical);
    EXPORT(gui_scrollbar_thumb_rect);

    /* Keyboard */
    EXPORT(kbd_next_event);
    EXPORT(kbd_inject_hid_usage);

    /* Kernel memory/string helpers */
    EXPORT(kmalloc);
    EXPORT(krealloc);
    EXPORT(kfree);
    EXPORT(kmemcpy);
    EXPORT(kmemcmp);
    EXPORT(kstrcmp);
    EXPORT(kmemset);
    EXPORT(mm_set_allocation_owner);
    EXPORT(mm_get_system_info);
    /* Boot snapshots are required by resident graphics drivers after page
     * zero has been unmapped. Export the snapshot accessors, never raw BDA
     * pointers. */
    EXPORT(mm_boot_equipment_word);
    EXPORT(mm_boot_ebda_segment);
    EXPORT(mm_boot_conventional_kb);
    EXPORT(mm_boot_vesa_read8);
    EXPORT(mm_boot_vesa_read16);
    EXPORT(mm_boot_vesa_read32);
    EXPORT(kprintf);
    EXPORT(kstrcat);
    EXPORT(kstrcpy);
    EXPORT(kstrncmp);
    EXPORT(kstrncpy);
    EXPORT(kstrlen);
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
    EXPORT(sound_has_sb16);
    EXPORT(sound_pcm_available);
    EXPORT(sound_pcm_is_busy);
    EXPORT(sound_pcm_name);
    EXPORT(sound_play_pcm_u8);
    EXPORT(sound_start_tone);
    EXPORT(sound_stop);

    /* Tasks */
    EXPORT(task_bind_window);
    EXPORT(task_create);
    EXPORT(task_count);
    EXPORT(task_current_pid);
    EXPORT(task_cpu_usage);
    EXPORT(task_exit);
    EXPORT(task_exit_requested);
    EXPORT(task_get);
    EXPORT(task_launch_arg);
    EXPORT(task_preempt_disable);
    EXPORT(task_preempt_enable);
    EXPORT(task_request_exit);
    EXPORT(task_set_memory_hint);
    EXPORT(task_sleep);
    EXPORT(task_state_name);
    EXPORT(task_yield);

    /* VFS */
    EXPORT(vfs_chdir);
    EXPORT(vfs_close);
    EXPORT(vfs_get_fs_info);
    EXPORT(vfs_get_mount_name);
    EXPORT(vfs_getcwd);
    EXPORT(vfs_has_cdrom);
    EXPORT(vfs_listdir);
    EXPORT(vfs_mkdir);
    EXPORT(vfs_remove);
    EXPORT(vfs_rename);
    EXPORT(vfs_get_space);
    EXPORT(vfs_get_space_path);
    EXPORT(bk_about_attach);
    EXPORT(bk_about_show);
    EXPORT(sound_play_file);
    EXPORT(startup_sound_enabled);
    EXPORT(startup_sound_set_enabled);
    EXPORT(vfs_mount);
    EXPORT(vfs_mount_default);
    EXPORT(vfs_open);
    EXPORT(vfs_read);
    EXPORT(vfs_read_all);
    EXPORT(vfs_write);
    EXPORT(vfs_write_all);

    /* Block / ISO helpers used by the external file browser */
    EXPORT(block_get);
    EXPORT(block_count);
    EXPORT(block_at);
    EXPORT(block_type_name);
    EXPORT(block_read);
    EXPORT(iso9660_mount_default);
    EXPORT(iso9660_is_mounted);
    EXPORT(iso9660_register_driver);
    EXPORT(ata_refresh_media);

    /* ABI Win32 para módulos residentes de compatibilidad. */
    EXPORT(win32_register_resolver);
    EXPORT(win32_user32_resolve);
    EXPORT(win32_kernel32_resolve);
    EXPORT(win32_file_write);

    /* ABI para controladores residentes .DVR. */
    EXPORT(sound_register_driver);
    EXPORT(rtc_register_driver);
    EXPORT(mouse_register_driver);
    EXPORT(mouse_inject_relative);
    EXPORT(mouse_inject_disconnect);
    EXPORT(lpt_register_virtual_provider);
    EXPORT(lpt_unregister_virtual_provider);
    EXPORT(usb_class_register);
    EXPORT(usb_class_unregister);
    EXPORT(usb_device_count);
    EXPORT(usb_device_at);
    EXPORT(usb_interface_endpoint);
    EXPORT(usb_control_transfer);
    EXPORT(usb_endpoint_transfer);
    EXPORT(usb_clear_endpoint_halt);
    EXPORT(usb_set_interface);
    EXPORT(usb_enumerate_hub_port);
    EXPORT(usb_disconnect_hub_port);
    EXPORT(vesa_register_driver);
    EXPORT(vesa_init_from_bootinfo);
    EXPORT(vesa_attach_lfb);
    EXPORT(vesa_fill_rect_rgb);
    EXPORT(gfx_register_driver);
    EXPORT(gfx3d_register_driver);
    EXPORT(gfx3d_available);
    EXPORT(gfx3d_get_info);
    EXPORT(gfx3d_driver_name);
    EXPORT(gfx3d_capabilities);
    EXPORT(gfx3d_surface_create);
    EXPORT(gfx3d_surface_destroy);
    EXPORT(gfx3d_surface_upload);
    EXPORT(gfx3d_surface_upload_region);
    EXPORT(gfx3d_surface_download);
    EXPORT(gfx3d_surface_clear);
    EXPORT(gfx3d_surface_composite);
    EXPORT(gfx3d_surface_present);
    EXPORT(gfx3d_depth_upload);
    EXPORT(gfx3d_depth_download);
    EXPORT(gfx3d_begin);
    EXPORT(gfx3d_draw_triangles);
    EXPORT(gfx3d_end);
    EXPORT(gfx3d_wait_fence);
    EXPORT(gfx3d_selftest);
    EXPORT(gfx3d_reset);
    EXPORT(r128_engine_register_2d);
    EXPORT(r128_engine_unregister_2d);
    EXPORT(r128_engine_acquire_2d);
    EXPORT(r128_engine_acquire_3d);
    EXPORT(r128_engine_release_2d);
    EXPORT(r128_engine_release_3d);
    EXPORT(r128_engine_present_vram32);
    EXPORT(r128_engine_reserved_vram_end);
    EXPORT(r128_engine_context);
    EXPORT(r128_engine_report);
    EXPORT(svga_transport_register);
    EXPORT(svga_transport_unregister);
    EXPORT(svga_transport_get);
    EXPORT(irq_install_handler);
    EXPORT(irq_uninstall_handler);
    EXPORT(driver_count);
    EXPORT(driver_at);
    EXPORT(driver_load);
    EXPORT(netdev_register);
    EXPORT(netdev_unregister);
    EXPORT(netdev_present);
    EXPORT(netdev_send);
    EXPORT(netdev_poll);
    EXPORT(netdev_receive);
    EXPORT(netdev_set_rx_handler);
    EXPORT(netdev_get_info);
    EXPORT(network_register_stack);
    EXPORT(network_unregister_stack);
    EXPORT(network_resolve);
    EXPORT(network_socket_open);
    EXPORT(network_socket_connect);
    EXPORT(network_socket_send);
    EXPORT(network_socket_receive);
    EXPORT(network_socket_close);
    EXPORT(network_http_get);
    EXPORT(network_register_tls);
    EXPORT(network_unregister_tls);

    /* Read-only hardware enumeration for Control Panel applets. */
    EXPORT(pci_device_count);
    EXPORT(pci_device_at);
    EXPORT(pci_class_name);
    EXPORT(pci_get_bar_info);
    EXPORT(pci_enable_command);
    EXPORT(pci_config_read16);
    EXPORT(pci_config_read32);
    EXPORT(pci_config_write16);
    EXPORT(pci_config_write32);

    /* VGA */
    EXPORT(vga_set_output_sink);

    /* GFX */
    EXPORT(gfx_get_info);
    EXPORT(gfx_list_display_modes);
    EXPORT(gfx_list_all_display_modes);
    EXPORT(gfx_overlay_supported);
    EXPORT(gfx_overlay_put);
    EXPORT(gfx_overlay_stop);

    /* Program helpers */
    EXPORT(deskmanager_set_background);
    EXPORT(deskmanager_get_background);
    EXPORT(deskmanager_get_wallpaper_path);
    EXPORT(deskmanager_set_wallpaper);
    EXPORT(deskmanager_get_wallpaper_mode);
    EXPORT(deskmanager_set_wallpaper_mode);
    EXPORT(elf_last_error);
    EXPORT(elf_preview_create);
    EXPORT(elf_preview_destroy);
    EXPORT(pe_dump_info);
    EXPORT(pe_execute_program);
    EXPORT(pe_last_error);
    EXPORT(program_execute_path);
    EXPORT(program_execute_path_arg);
    EXPORT(program_draw_icon_pixels);
    EXPORT(program_is_object);
    EXPORT(program_is_win32_executable);
    EXPORT(program_launch_arg);
    EXPORT(program_load_bmp_icon_scaled);
    EXPORT(program_load_bmp_wallpaper_scaled);
    EXPORT(shell_execute_line);
    EXPORT(mouse_set_sensitivity);
    EXPORT(mouse_get_sensitivity);
    EXPORT(screensaver_get_path);
    EXPORT(screensaver_get_timeout_seconds);
    EXPORT(screensaver_is_enabled);
    EXPORT(screensaver_preview);
    EXPORT(screensaver_set_enabled);
    EXPORT(screensaver_set_path);
    EXPORT(screensaver_set_timeout_seconds);

    return 0;
}

static bool elf_symbol_is_shared_data(const char *name) {
    return kstrcmp(name, "errno") == 0 || kstrcmp(name, "stderr") == 0 ||
           kstrcmp(name, "stdout") == 0;
}

static uint32_t elf_user_shared_data(const char *name, uint32_t target) {
    uint32_t **published;
    uint32_t *candidate;
    uint32_t flags;
    uint32_t result;

    if (!name || !target) return target;
    if (kstrcmp(name, "errno") == 0) published = &g_user_errno_slot;
    else if (kstrcmp(name, "stdout") == 0) published = &g_user_stdout_slot;
    else if (kstrcmp(name, "stderr") == 0) published = &g_user_stderr_slot;
    else return target;

    candidate = (uint32_t *)kmalloc(sizeof(uint32_t));
    if (!candidate) return 0U;
    (void)mm_set_allocation_owner(candidate, 0U);
    *candidate = *(const uint32_t *)(uintptr_t)target;

    flags = kspin_lock_irqsave(&g_user_thunk_lock);
    if (!*published) {
        __asm__ volatile ("" : : : "memory");
        *published = candidate;
        candidate = NULL;
    }
    result = (uint32_t)(uintptr_t)*published;
    kspin_unlock_irqrestore(&g_user_thunk_lock, flags);
    if (candidate) kfree(candidate);
    return result;
}

static void elf_user_api_set_metadata(uint32_t token, const char *name,
                                      uint32_t domains, bool merge_domains) {
    char *label;
    if (token >= ELF_USER_THUNK_MAX) return;
    if (merge_domains) g_user_api_domains[token] |= domains;
    else g_user_api_domains[token] = domains;
    label = g_user_api_labels[token];
    if (label[0] || !name || !name[0]) return;
    kstrncpy(label, name, ELF_USER_API_LABEL_MAX - 1U);
    label[ELF_USER_API_LABEL_MAX - 1U] = '\0';
}

uint32_t elf_user_api_thunk(const char *name, uint32_t target) {
    uint8_t *code;
    uint8_t *candidate;
    uint32_t token;
    uint32_t domains;
    uint32_t send_message_thunk;
    uint32_t dialog_thunk;
    uint32_t flags;

    if (!target) return target;
    domains = kernel_domain_mask_for_api(name);
    if (elf_symbol_is_shared_data(name))
        return elf_user_shared_data(name, target);
    send_message_thunk = elf_user_send_message_thunk(name);
    if (send_message_thunk) return send_message_thunk;
    dialog_thunk = elf_user_dialog_thunk(name);
    if (dialog_thunk) return dialog_thunk;

    /* Phase 1 made kernel .text/.bss supervisor-only. The old static thunk
     * matrix therefore became non-executable from CPL3. Build a candidate in
     * the user-visible heap first, then publish it under a short lock. */
    candidate = (uint8_t *)kmalloc(ELF_USER_THUNK_SIZE);
    if (!candidate) return 0U;
    (void)mm_set_allocation_owner(candidate, 0U);
    kmemset(candidate, 0x90, ELF_USER_THUNK_SIZE);

    flags = kspin_lock_irqsave(&g_user_thunk_lock);
    for (token = 0U; token < g_user_thunk_count; token++) {
        if (g_user_targets[token] == target && g_user_thunks[token]) {
            uint32_t existing = (uint32_t)(uintptr_t)g_user_thunks[token];
            /* Aliases may classify the same target more conservatively.  Keep
             * the union so reusing a thunk can never remove synchronization. */
            elf_user_api_set_metadata(token, name, domains, true);
            kspin_unlock_irqrestore(&g_user_thunk_lock, flags);
            kfree(candidate);
            return existing;
        }
    }
    if (g_user_thunk_count >= ELF_USER_THUNK_MAX) {
        kspin_unlock_irqrestore(&g_user_thunk_lock, flags);
        kfree(candidate);
        return 0U;
    }

    token = g_user_thunk_count++;
    g_user_targets[token] = target;
    elf_user_api_set_metadata(token, name, domains, false);
    code = candidate;

    /* WIN32_RING3_EH_PROLOG_THUNK
     * _EH_prolog rewrites the caller stack and FS:[0], so it must execute
     * directly on the Ring-3 stack instead of through SYS_API_CALL. */
    if (name && kstrcmp(name, "_EH_prolog") == 0) {
        static const uint8_t eh_prolog[] = {
            0x6A, 0xFF,
            0x50,
            0x64, 0xFF, 0x35, 0, 0, 0, 0,
            0x64, 0x89, 0x25, 0, 0, 0, 0,
            0x8B, 0x44, 0x24, 0x0C,
            0x89, 0x6C, 0x24, 0x0C,
            0x8D, 0x6C, 0x24, 0x0C,
            0x50,
            0xC3
        };
        kmemcpy(code, eh_prolog, sizeof(eh_prolog));
    } else {
        code[0] = 0xB8; /* mov eax, SYS_API_CALL */
        *(uint32_t *)(void *)(code + 1) = SYS_API_CALL;
        /* ECX es caller-saved en i386. EBX no lo es: usarlo para el token
         * corrompia el estado que las aplicaciones conservan entre llamadas. */
        code[5] = 0xB9; /* mov ecx, token */
        *(uint32_t *)(void *)(code + 6) = token;
        code[10] = 0xCD; code[11] = 0x80; /* int 0x80 */

        /* Native ELF/BEX code has a flat-segment ABI. Reload the visible
         * selectors at the user-side boundary and preserve EAX. */
        code[12] = 0x50;                  /* push eax */
        code[13] = 0x66; code[14] = 0xB8; /* mov ax, GDT_USER_DATA */
        code[15] = (uint8_t)GDT_USER_DATA; code[16] = 0x00;
        code[17] = 0x8E; code[18] = 0xD8; /* mov ds, ax */
        code[19] = 0x8E; code[20] = 0xC0; /* mov es, ax */
        code[21] = 0x8E; code[22] = 0xE0; /* mov fs, ax */
        code[23] = 0x8E; code[24] = 0xE8; /* mov gs, ax */
        code[25] = 0x58;                  /* pop eax */
        code[26] = 0xC3;                  /* ret */
    }

    /* Publish last: a concurrent loader must never observe half-written
     * executable bytes. x86 preserves store order, and the compiler barrier
     * prevents C reordering across the pointer publication. */
    __asm__ volatile ("" : : : "memory");
    g_user_thunks[token] = code;
    kspin_unlock_irqrestore(&g_user_thunk_lock, flags);
    return (uint32_t)(uintptr_t)code;
}

uint64_t elf_user_api_dispatch(uint32_t token, const uint32_t *a,
                               bool *valid, uint32_t *callee_cleanup) {
    extern uint64_t elf_api_call_raw(uint32_t target, const uint32_t *args,
                                     uint32_t *callee_cleanup);
    const char *api_name;
    uint32_t target;
    uint64_t result;

    if (valid) *valid = false;
    if (!a || token >= g_user_thunk_count || !g_user_targets[token]) return 0;
    if (valid) *valid = true;

    api_name = g_user_api_labels[token][0]
        ? g_user_api_labels[token] : "(API sin nombre)";
    target = g_user_targets[token];
    g_last_user_api_name = api_name;
    {
        uint32_t domains = g_user_api_domains[token];
        kernel_domains_enter(domains);
        task_user_api_guard_enter(api_name, target, token);
        result = elf_api_call_raw(target, a, callee_cleanup);
        if (g_user_errno_slot) *g_user_errno_slot = (uint32_t)errno;
        task_user_api_guard_leave();
        kernel_domains_exit(domains);
    }
    return result;
}

const char *elf_last_user_api_name(void) {
    const char *name = NULL;
    if (task_user_api_guard_info(&name, NULL, NULL) && name) return name;
    return g_last_user_api_name;
}

bool elf_user_api_fault_info(const char **name_out, uint32_t *target_out,
                             uint32_t *token_out) {
    return task_user_api_guard_info(name_out, target_out, token_out);
}

void elf_user_api_fault_clear(void) {
    task_user_api_guard_clear();
}

bool elf_user_api_thunk_info(uint32_t address, const char **name_out,
                             uint32_t *target_out, uint32_t *token_out) {
    uint32_t flags;
    bool found = false;

    if (name_out) *name_out = NULL;
    if (target_out) *target_out = 0U;
    if (token_out) *token_out = 0U;

    flags = kspin_lock_irqsave(&g_user_thunk_lock);
    for (uint32_t token = 0U; token < g_user_thunk_count; token++) {
        uint32_t begin;
        if (!g_user_thunks[token]) continue;
        begin = (uint32_t)(uintptr_t)g_user_thunks[token];
        if (address < begin || address >= begin + ELF_USER_THUNK_SIZE)
            continue;
        if (name_out)
            *name_out = g_user_api_labels[token][0]
                ? g_user_api_labels[token] : "(API sin nombre)";
        if (target_out) *target_out = g_user_targets[token];
        if (token_out) *token_out = token;
        found = true;
        break;
    }
    kspin_unlock_irqrestore(&g_user_thunk_lock, flags);
    return found;
}


static uint32_t elf_symbol_value(const elf32_symbol_t *symbol,
                                 const char *strings,
                                 const uint32_t *section_addresses,
                                 uint16_t section_count, bool user_image) {
    if (symbol->shndx == SHN_UNDEF) {
        const char *name = strings + symbol->name;
        uint32_t target = elf_kernel_symbol(name);
        return user_image ? elf_user_api_thunk(name, target) : target;
    }
    if (symbol->shndx == SHN_ABS) return symbol->value;
    if (symbol->shndx >= section_count ||
        section_addresses[symbol->shndx] == 0) return 0;
    return section_addresses[symbol->shndx] + symbol->value;
}

static bool elf_validate(const elf32_header_t *header, uint32_t size) {
    uint32_t section_bytes;

    if (!header || size < sizeof(*header)) {
        g_elf_error = "ELF truncado: falta el encabezado";
        return false;
    }
    if (header->ident[0] != 0x7F || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F') {
        g_elf_error = "firma ELF invalida; archivo incorrecto o corrupto";
        return false;
    }
    if (header->ident[4] != 1 || header->ident[5] != 1) {
        g_elf_error = "ELF incompatible: se requiere ELF32 little-endian";
        return false;
    }
    if (header->type != ET_REL) {
        g_elf_error = "tipo ELF incompatible: se requiere ET_REL";
        return false;
    }
    if (header->machine != EM_386) {
        g_elf_error = "arquitectura ELF incompatible: se requiere i386";
        return false;
    }
    if (header->version != EV_CURRENT) {
        g_elf_error = "version ELF invalida";
        return false;
    }
    if (header->shentsize != sizeof(elf32_section_t) || header->shnum == 0) {
        g_elf_error = "tabla de secciones ELF invalida";
        return false;
    }
    section_bytes = (uint32_t)header->shnum * header->shentsize;
    if (!elf_range_ok(header->shoff, section_bytes, size)) {
        kprintf("[ELF] archivo truncado: size=%u shoff=%u shnum=%u shentsize=%u end=%u\n",
                size, header->shoff, header->shnum, header->shentsize,
                header->shoff + section_bytes);
        g_elf_error = "ELF truncado: copia incompleta en el disco";
        return false;
    }
    return true;
}

static bool elf_load(const uint8_t *file, uint32_t file_size,
                     const char *entry_name, void **image_out,
                     void **entry_out, bool user_image) {
    const elf32_header_t *header = (const elf32_header_t *)file;
    const elf32_section_t *sections;
    uint32_t *addresses;
    uint8_t *image;
    uint8_t *raw_image;
    uint32_t image_size = 0;
    uint32_t image_alignment = sizeof(void *);
    const elf32_section_t *symtab_section = NULL;
    const elf32_symbol_t *symbols = NULL;
    const char *strings = NULL;
    uint32_t symbol_count = 0;

    if (!entry_name || !image_out || !entry_out) {
        g_elf_error = "argumentos del cargador ELF invalidos";
        return false;
    }
    if (!elf_validate(header, file_size)) return false;
    sections = (const elf32_section_t *)(file + header->shoff);
    addresses = (uint32_t *)kzalloc(header->shnum * sizeof(uint32_t));
    if (!addresses) {
        g_elf_error = "sin memoria para secciones ELF";
        return false;
    }

    for (uint16_t i = 0; i < header->shnum; i++) {
        if (!(sections[i].flags & SHF_ALLOC)) continue;
        if (sections[i].addralign > image_alignment)
            image_alignment = sections[i].addralign;
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
    if (image_alignment & (image_alignment - 1U)) {
        kfree(addresses);
        g_elf_error = "alineacion ELF invalida";
        return false;
    }
    raw_image = (uint8_t *)kzalloc(image_size + image_alignment - 1U +
                                   sizeof(void *));
    if (!raw_image) {
        kfree(addresses);
        g_elf_error = "sin memoria para cargar programa";
        return false;
    }
    image = (uint8_t *)elf_align(
        (uint32_t)(uintptr_t)(raw_image + sizeof(void *)), image_alignment);
    ((void **)image)[-1] = raw_image;
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
                             header->shnum, user_image) == 0 &&
            ELF32_ST_BIND(symbols[i].info) != STB_WEAK) {
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
                                            addresses, header->shnum,
                                            user_image);
            if (type == R_386_NONE) {
                /* Standard no-op relocation emitted for discarded weak
                   references by ld -r. */
            } else if (type == R_386_32) {
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
        if (symbols[i].shndx != SHN_UNDEF && symbols[i].name != 0 &&
            kstrcmp(strings + symbols[i].name, entry_name) == 0) {
            uint32_t value = elf_symbol_value(&symbols[i], strings, addresses,
                                              header->shnum, user_image);
            if (!value) break;
            *image_out = image;
            *entry_out = (void *)(uintptr_t)value;
            kfree(addresses);
            return true;
        }
    }
    g_elf_error = "falta el punto de entrada ELF";
    goto fail;

malformed:
    g_elf_error = "estructura ELF malformada";
fail:
    kfree(raw_image);
    kfree(addresses);
    return false;
}

/* Cargador ET_REL de bajo pico de memoria. El camino tradicional necesita el
 * archivo completo y la imagen final simultaneamente. En 8 MiB leemos sólo
 * las secciones alloc, las tablas de símbolos y pequeños lotes de relocations. */
static bool elf_read_exact(int fd, uint32_t offset, void *buffer,
                           uint32_t size) {
    int got;
    if (!buffer || !size || offset > 0x7FFFFFFFU) return false;
    if (vfs_seek(fd, (int32_t)offset, 0U) != (int32_t)offset) return false;
    got = vfs_read(fd, buffer, size);
    return got >= 0 && (uint32_t)got == size;
}

static bool elf_load_streamed(const char *path, const char *entry_name,
                              void **image_out, void **entry_out,
                              bool user_image) {
    elf32_header_t header;
    elf32_section_t *sections = NULL;
    elf32_symbol_t *symbols = NULL;
    elf32_rel_t *relocations = NULL;
    char *strings = NULL;
    uint32_t *addresses = NULL;
    uint8_t *raw_image = NULL;
    uint8_t *image;
    uint32_t image_size = 0U;
    uint32_t image_alignment = sizeof(void *);
    uint32_t symbol_count = 0U;
    uint32_t string_size = 0U;
    uint16_t symtab_index = 0xFFFFU;
    uint32_t file_size;
    int fd = -1;
    bool result = false;

    if (image_out) *image_out = NULL;
    if (entry_out) *entry_out = NULL;
    if (!path || !entry_name || !image_out || !entry_out) {
        g_elf_error = "argumentos del cargador ELF streaming invalidos";
        return false;
    }
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        g_elf_error = "no se pudo abrir el programa";
        return false;
    }
    {
        int32_t measured = vfs_size(fd);
        if (measured <= 0) {
            g_elf_error = "tamano ELF invalido";
            goto done;
        }
        file_size = (uint32_t)measured;
    }
    if (!elf_read_exact(fd, 0U, &header, sizeof(header)) ||
        !elf_validate(&header, file_size)) goto done;

    sections = (elf32_section_t *)kmalloc(
        (size_t)header.shnum * sizeof(*sections));
    addresses = (uint32_t *)kzalloc(
        (size_t)header.shnum * sizeof(*addresses));
    if (!sections || !addresses ||
        !elf_read_exact(fd, header.shoff, sections,
                        (uint32_t)header.shnum * sizeof(*sections))) {
        g_elf_error = "sin memoria para tabla de secciones ELF";
        goto done;
    }

    for (uint16_t i = 0U; i < header.shnum; i++) {
        if (!(sections[i].flags & SHF_ALLOC)) continue;
        if (sections[i].addralign > image_alignment)
            image_alignment = sections[i].addralign;
        image_size = elf_align(image_size,
            sections[i].addralign ? sections[i].addralign : 1U);
        addresses[i] = image_size;
        if (sections[i].size > 0xFFFFFFFFU - image_size) {
            g_elf_error = "imagen ELF demasiado grande";
            goto done;
        }
        image_size += sections[i].size;
    }
    if (!image_size || (image_alignment & (image_alignment - 1U))) {
        g_elf_error = "alineacion ELF invalida";
        goto done;
    }
    raw_image = (uint8_t *)kzalloc(image_size + image_alignment - 1U +
                                   sizeof(void *));
    if (!raw_image) {
        g_elf_error = "sin memoria para imagen ELF streaming";
        goto done;
    }
    image = (uint8_t *)elf_align(
        (uint32_t)(uintptr_t)(raw_image + sizeof(void *)), image_alignment);
    ((void **)image)[-1] = raw_image;

    for (uint16_t i = 0U; i < header.shnum; i++) {
        if (!(sections[i].flags & SHF_ALLOC)) continue;
        addresses[i] += (uint32_t)(uintptr_t)image;
        if (sections[i].type == SHT_NOBITS || !sections[i].size) continue;
        if (!elf_range_ok(sections[i].offset, sections[i].size, file_size) ||
            !elf_read_exact(fd, sections[i].offset,
                            (void *)(uintptr_t)addresses[i],
                            sections[i].size)) {
            g_elf_error = "seccion ELF fuera del archivo";
            goto done;
        }
    }

    for (uint16_t i = 0U; i < header.shnum; i++) {
        if (sections[i].type != SHT_SYMTAB) continue;
        if (sections[i].entsize != sizeof(elf32_symbol_t) ||
            sections[i].link >= header.shnum ||
            !elf_range_ok(sections[i].offset, sections[i].size, file_size)) {
            g_elf_error = "tabla de simbolos ELF invalida";
            goto done;
        }
        elf32_section_t *strtab = &sections[sections[i].link];
        if (!elf_range_ok(strtab->offset, strtab->size, file_size)) {
            g_elf_error = "tabla de cadenas ELF invalida";
            goto done;
        }
        symbols = (elf32_symbol_t *)kmalloc(sections[i].size);
        strings = (char *)kmalloc(strtab->size + 1U);
        if (!symbols || !strings ||
            !elf_read_exact(fd, sections[i].offset, symbols,
                            sections[i].size) ||
            !elf_read_exact(fd, strtab->offset, strings, strtab->size)) {
            g_elf_error = "sin memoria para simbolos ELF";
            goto done;
        }
        strings[strtab->size] = '\0';
        string_size = strtab->size;
        symbol_count = sections[i].size / sizeof(*symbols);
        symtab_index = i;
        break;
    }
    if (!symbols || !strings) {
        g_elf_error = "ELF sin tabla de simbolos";
        goto done;
    }

    for (uint32_t i = 0U; i < symbol_count; i++) {
        if (symbols[i].name >= string_size) {
            g_elf_error = "nombre de simbolo ELF fuera de rango";
            goto done;
        }
        if (symbols[i].shndx == SHN_UNDEF && symbols[i].name != 0U &&
            elf_symbol_value(&symbols[i], strings, addresses,
                             header.shnum, user_image) == 0U &&
            ELF32_ST_BIND(symbols[i].info) != STB_WEAK) {
            kprintf("[ELF] simbolo no resuelto: %s\n",
                    strings + symbols[i].name);
            g_elf_error = "simbolo externo no resuelto";
            goto done;
        }
    }

    for (uint16_t i = 0U; i < header.shnum; i++) {
        elf32_section_t *relsec = &sections[i];
        if (relsec->type != SHT_REL) continue;
        if (relsec->info >= header.shnum || relsec->link != symtab_index ||
            relsec->entsize != sizeof(elf32_rel_t) ||
            !addresses[relsec->info] ||
            !elf_range_ok(relsec->offset, relsec->size, file_size)) {
            g_elf_error = "seccion de relocaciones ELF invalida";
            goto done;
        }
        /* Leer la seccion de una vez cuesta como maximo unas decenas de KiB
           en los programas actuales. En FAT32 evita volver a recorrer desde
           el primer cluster por cada bloque de 1 KiB y elimina una carrera
           con otros lectores VFS durante los yields de esa caminata. */
        relocations = (elf32_rel_t *)kmalloc(relsec->size);
        if (!relocations || !elf_read_exact(fd, relsec->offset, relocations,
                                            relsec->size)) {
            g_elf_error = "no se pudieron leer relocaciones ELF";
            goto done;
        }
        for (uint32_t r = 0U; r < relsec->size / sizeof(*relocations); r++) {
                uint32_t symbol_index = relocations[r].info >> 8U;
                uint8_t type = (uint8_t)(relocations[r].info & 0xFFU);
                uint32_t target_size = sections[relsec->info].size;
                uint32_t *place;
                uint32_t symbol_value;
                if (symbol_index >= symbol_count || target_size < 4U ||
                    relocations[r].offset > target_size - 4U) {
                    g_elf_error = "relocacion ELF fuera de rango";
                    goto done;
                }
                place = (uint32_t *)(uintptr_t)
                    (addresses[relsec->info] + relocations[r].offset);
                symbol_value = elf_symbol_value(&symbols[symbol_index],
                    strings, addresses, header.shnum, user_image);
                if (type == R_386_NONE) { /* no relocation */ }
                else if (type == R_386_32) *place += symbol_value;
                else if (type == R_386_PC32)
                    *place += symbol_value - (uint32_t)(uintptr_t)place;
                else {
                    g_elf_error = "tipo de relocacion ELF no soportado";
                    goto done;
                }
        }
        kfree(relocations);
        relocations = NULL;
    }

    for (uint32_t i = 0U; i < symbol_count; i++) {
        if (symbols[i].shndx != SHN_UNDEF && symbols[i].name != 0U &&
            symbols[i].name < string_size &&
            kstrcmp(strings + symbols[i].name, entry_name) == 0) {
            uint32_t value = elf_symbol_value(&symbols[i], strings, addresses,
                                              header.shnum, user_image);
            if (!value) break;
            *image_out = image;
            *entry_out = (void *)(uintptr_t)value;
            raw_image = NULL;
            result = true;
            kprintf("[ELF:LOW] %s cargado por secciones: imagen=%u KiB\n",
                    path, image_size / 1024U);
            break;
        }
    }
    if (!result) g_elf_error = "falta el punto de entrada ELF";

done:
    if (fd >= 0) vfs_close(fd);
    if (raw_image) kfree(raw_image);
    if (strings) kfree(strings);
    if (symbols) kfree(symbols);
    if (relocations) kfree(relocations);
    if (addresses) kfree(addresses);
    if (sections) kfree(sections);
    return result;
}

bool elf_preview_create(const char *path, gui_desktop_t *desktop,
                        void **image_out) {
    void *file = NULL;
    void *image = NULL;
    uint32_t size = 0;
    elf_program_entry_t entry = NULL;

    if (image_out) *image_out = NULL;
    if (!path || !desktop || !image_out) {
        g_elf_error = "argumentos de preview invalidos";
        return false;
    }
    if (!vfs_read_all(path, &file, &size)) {
        g_elf_error = "no se pudo leer el preview";
        return false;
    }
    if (!elf_load((const uint8_t *)file, size, "bleskernos_program_main",
                  &image, (void **)&entry, false)) {
        kfree(file);
        return false;
    }
    kfree(file);

    /* El entrypoint solo registra su gui_program sobre el desktop virtual.
       Se ejecuta sin crear una tarea para que el preview quede listo ahora. */
    entry(desktop);
    *image_out = image;
    return true;
}

void elf_preview_destroy(void *image) {
    elf_release_image(image);
}

bool elf_execute_program(const char *path, gui_desktop_t *desktop) {
    return elf_execute_program_ex(path, desktop, NULL);
}

static const char *elf_task_name(const char *path) {
    const char *name = path;

    if (!path) return "program";
    while (*path) {
        if (*path == '/' || *path == '\\') name = path + 1;
        path++;
    }
    return *name ? name : "program";
}

bool elf_execute_program_ex(const char *path, gui_desktop_t *desktop,
                            const char *launch_arg) {
    return elf_spawn_program_ex(path, desktop, launch_arg) >= 0;
}

int elf_spawn_program_ex(const char *path, gui_desktop_t *desktop,
                         const char *launch_arg) {
    void *file = NULL;
    void *image = NULL;
    elf_program_entry_t entry = NULL;
    uint32_t size = 0;
    uint32_t image_slot = TASK_MAX;
    int pid;
#if ELF_LOAD_TIMING
    uint32_t timing_start = pit_get_ticks();
    uint32_t timing_read;
    uint32_t timing_link;
#endif

    if (!path || !desktop) return -1;
    if (compat_mode_is_low_memory()) {
        if (!elf_load_streamed(path, "bleskernos_program_main",
                               &image, (void **)&entry, true)) return -1;
    } else {
        if (!vfs_read_all(path, &file, &size)) {
            g_elf_error = "no se pudo leer el programa";
            return -1;
        }
#if ELF_LOAD_TIMING
        timing_read = pit_get_ticks();
#endif
        if (!elf_load((const uint8_t *)file, size, "bleskernos_program_main",
                      &image, (void **)&entry, true)) {
            kfree(file);
            return -1;
        }
        kfree(file);
        file = NULL;
    }
#if ELF_LOAD_TIMING
    if (compat_mode_is_low_memory()) {
        timing_read = timing_start;
        size = 0U;
    }
    timing_link = pit_get_ticks();
#endif
    for (uint32_t i = 0U; i < TASK_MAX; i++)
        if (!g_process_images[i].process_id) { image_slot = i; break; }
    if (image_slot == TASK_MAX) {
        elf_release_image(image);
        g_elf_error = "sin slots para registrar imagen ELF";
        return -1;
    }
    /* The real gui_desktop_t is supervisor-only. Native programs receive the
     * ABI-compatible user view returned by bk_gui_desktop(); API entry points
     * translate that handle back to the real desktop. */
    pid = task_create_user_program(elf_task_name(path),
                                   (task_entry_t)entry,
                                   bk_gui_desktop(), launch_arg);
    if (pid < 0) {
        elf_release_image(image);
        g_elf_error = "sin slots para crear proceso";
        return -1;
    }
    g_process_images[image_slot].process_id = (uint32_t)pid;
    g_process_images[image_slot].image = image;
    (void)mm_set_allocation_owner(((void **)image)[-1], (uint32_t)pid);
#if ELF_LOAD_TIMING
    kprintf("[ELF:PERF] %s bytes=%u read=%u link=%u create=%u ticks\n",
            path, size, timing_read - timing_start,
            timing_link - timing_read, pit_get_ticks() - timing_link);
#endif
    return pid;
}

bool elf_load_resident(const char *path, const char *entry_symbol,
                       void **image_out, void **entry_out) {
    void *file = NULL;
    uint32_t size = 0;

    if (image_out) *image_out = NULL;
    if (entry_out) *entry_out = NULL;
    if (!path || !entry_symbol || !image_out || !entry_out) {
        g_elf_error = "argumentos de modulo invalidos";
        return false;
    }
    if (!vfs_read_all(path, &file, &size)) {
        g_elf_error = "no se pudo leer el modulo";
        return false;
    }
    if (!elf_load((const uint8_t *)file, size, entry_symbol,
                  image_out, entry_out, false)) {
        kfree(file);
        return false;
    }
    kfree(file);
    return true;
}

void elf_release_image(void *image) {
    if (image) kfree(((void **)image)[-1]);
}

void elf_process_cleanup(uint32_t process_id) {
    if (!process_id) return;
    for (uint32_t i = 0U; i < TASK_MAX; i++) {
        if (g_process_images[i].process_id != process_id) continue;
        elf_release_image(g_process_images[i].image);
        g_process_images[i].process_id = 0U;
        g_process_images[i].image = NULL;
    }
}

bool elf_process_address(uint32_t process_id, uint32_t address,
                         uint32_t *base_out, uint32_t *offset_out) {
    if (!process_id) return false;
    for (uint32_t i=0U;i<TASK_MAX;i++) {
        uint32_t base;
        if (g_process_images[i].process_id != process_id ||
            !g_process_images[i].image) continue;
        base=(uint32_t)(uintptr_t)g_process_images[i].image;
        if(address<base) return false;
        if(base_out)*base_out=base;
        if(offset_out)*offset_out=address-base;
        return true;
    }
    return false;
}

const char *elf_last_error(void) {
    return g_elf_error;
}
