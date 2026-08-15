#include "include/syscall.h"
#include "include/task.h"
#include "include/gdt.h"
#include "include/kernel_domains.h"
#include "include/pit.h"
#include "include/vga.h"
#include "include/vfs.h"
#include "include/memory.h"
#include "include/usercopy.h"
#include "include/elf_loader.h"
#include "include/pe_loader.h"
#include "include/ne_loader.h"
#include "include/network.h"
#include "../gui/gui.h"
#include "win32/exception.h"

#define SYSCALL_WRITE_MAX       65536U
#define SYSCALL_ALLOC_MAX       (64U * 1024U * 1024U)
#define SYSCALL_RESOURCE_SLOTS  16
#define SYSCALL_FD_SLOTS        VFS_MAX_OPEN_FILES
#define SYSCALL_ALLOC_SLOTS     128
#define SYS_API_MAX_CALLEE_CLEANUP 64U
#define SYS_API_KERNEL_STACK_RESERVE (16U * 1024U)

typedef struct {
    uint32_t process_id;
    int fds[SYSCALL_FD_SLOTS];
    void *allocations[SYSCALL_ALLOC_SLOTS];
    int net_sockets[NET_SOCKET_MAX];
} syscall_resources_t;

static syscall_resources_t resources[SYSCALL_RESOURCE_SLOTS];

static bool caller_is_user(const registers_t *regs) {
    return regs && (regs->cs & 3U) == 3U;
}

/* BLES_WINE_LOW_PE_RETURN_ADDRESS_V2_20260723
 *
 * Las imágenes PE con vista fija pueden vivir por debajo de HEAP_START
 * (por ejemplo WINZIP32.EXE en 0x00400000). Un wrapper stdcall puede retornar
 * legítimamente a ese código, así que también debemos aceptar direcciones
 * pertenecientes a una imagen PE cargada.
 */
static bool syscall_win32_return_address_ok(uint32_t address) {
    const uint8_t *image_base = NULL;
    uint32_t image_size = 0U;

    if (address >= HEAP_START && address < HEAP_END)
        return true;

    return pe_win32_query_image_region(
        (const void *)(uintptr_t)address,
        &image_base,
        &image_size);
}

/* All user pointers are now checked against the actual page tables. Kernel
 * callers keep the historical direct-pointer path, while Ring-3 callers may
 * only touch present PAGE_USER mappings and must additionally have writable
 * permission for output buffers. */
static bool user_range_ok(const registers_t *regs, const void *pointer,
                          uint32_t length) {
    if (!caller_is_user(regs)) return pointer != NULL || length == 0U;
    return user_access_ok(pointer, length, false);
}

static bool user_write_range_ok(const registers_t *regs, void *pointer,
                                uint32_t length) {
    if (!caller_is_user(regs)) return pointer != NULL || length == 0U;
    return user_access_ok(pointer, length, true);
}

static bool copy_user_string(const registers_t *regs, const char *source,
                             char *destination, uint32_t capacity) {
    if (!source || !destination || capacity < 2U) return false;
    if (caller_is_user(regs))
        return copy_string_from_user(destination, capacity, source);
    for (uint32_t i = 0U; i < capacity; i++) {
        destination[i] = source[i];
        if (!destination[i]) return true;
    }
    destination[capacity - 1U] = '\0';
    return false;
}

static syscall_resources_t *resource_for(uint32_t process_id, bool create) {
    syscall_resources_t *free_slot = NULL;

    for (uint32_t i = 0; i < SYSCALL_RESOURCE_SLOTS; i++) {
        if (resources[i].process_id == process_id) return &resources[i];
        if (!resources[i].process_id && !free_slot) free_slot = &resources[i];
    }
    if (!create || !free_slot) return NULL;
    kmemset(free_slot, 0, sizeof(*free_slot));
    free_slot->process_id = process_id;
    for (uint32_t i = 0; i < SYSCALL_FD_SLOTS; i++) free_slot->fds[i] = -1;
    for (uint32_t i = 0; i < NET_SOCKET_MAX; i++) free_slot->net_sockets[i] = -1;
    return free_slot;
}

void syscall_process_cleanup(uint32_t process_id) {
    syscall_resources_t *resource = resource_for(process_id, false);
    if (!resource) return;
    for (uint32_t i = 0; i < SYSCALL_FD_SLOTS; i++) {
        if (resource->fds[i] >= 0) (void)vfs_close(resource->fds[i]);
    }
    for (uint32_t i = 0; i < SYSCALL_ALLOC_SLOTS; i++) {
        if (resource->allocations[i]) kfree(resource->allocations[i]);
    }
    for (uint32_t i = 0; i < NET_SOCKET_MAX; i++) {
        if (resource->net_sockets[i] >= 0)
            network_socket_close(resource->net_sockets[i]);
    }
    kmemset(resource, 0, sizeof(*resource));
}

static int resource_fd(syscall_resources_t *resource, uint32_t user_fd) {
    uint32_t slot;
    if (!resource || user_fd < 3U) return -1;
    slot = user_fd - 3U;
    return slot < SYSCALL_FD_SLOTS ? resource->fds[slot] : -1;
}

static int resource_socket(syscall_resources_t *resource, uint32_t handle) {
    if (!resource || handle >= NET_SOCKET_MAX) return -1;
    return resource->net_sockets[handle];
}

static int32_t sys_net_socket(uint32_t type) {
    syscall_resources_t *resource =
        resource_for(task_current_process_id(), true);
    int raw_socket;
    if (!resource || type != NET_SOCKET_TCP) return -BK_EINVAL;
    for (uint32_t i = 0; i < NET_SOCKET_MAX; i++) {
        if (resource->net_sockets[i] >= 0) continue;
        raw_socket = network_socket_open((uint8_t)type);
        if (raw_socket < 0) return -BK_EMFILE;
        resource->net_sockets[i] = raw_socket;
        return (int32_t)i;
    }
    return -BK_EMFILE;
}

static int32_t sys_net_close(uint32_t handle) {
    syscall_resources_t *resource =
        resource_for(task_current_process_id(), false);
    int raw_socket = resource_socket(resource, handle);
    if (raw_socket < 0) return -BK_EBADF;
    network_socket_close(raw_socket);
    resource->net_sockets[handle] = -1;
    return 0;
}

static int32_t sys_open(const registers_t *regs, const char *user_path,
                        uint32_t flags) {
    char path[VFS_MAX_PATH];
    syscall_resources_t *resource;
    int raw_fd;

    if (!copy_user_string(regs, user_path, path, sizeof(path)))
        return -BK_EFAULT;
    if ((flags & VFS_O_RDWR) == 0U || (flags & VFS_O_RDWR) > VFS_O_RDWR)
        return -BK_EINVAL;
    resource = resource_for(task_current_process_id(), true);
    if (!resource) return -BK_EMFILE;
    for (uint32_t i = 0; i < SYSCALL_FD_SLOTS; i++) {
        if (resource->fds[i] >= 0) continue;
        raw_fd = vfs_open(path, flags);
        if (raw_fd < 0) return -BK_ENOENT;
        resource->fds[i] = raw_fd;
        return (int32_t)(i + 3U);
    }
    return -BK_EMFILE;
}

static int32_t sys_read(const registers_t *regs, uint32_t fd, void *buffer,
                        uint32_t length) {
    syscall_resources_t *resource;
    int raw_fd;
    if (length > SYSCALL_WRITE_MAX ||
        !user_write_range_ok(regs, buffer, length))
        return -BK_EFAULT;
    resource = resource_for(task_current_process_id(), false);
    raw_fd = resource_fd(resource, fd);
    if (raw_fd < 0) return -BK_EBADF;
    return vfs_read(raw_fd, buffer, length);
}

static int32_t sys_write(const registers_t *regs, uint32_t fd,
                         const void *buffer, uint32_t length) {
    syscall_resources_t *resource;
    int raw_fd;
    if (length > SYSCALL_WRITE_MAX || !user_range_ok(regs, buffer, length))
        return -BK_EFAULT;
    if (fd == 1U || fd == 2U) {
        const char *text = (const char *)buffer;
        for (uint32_t i = 0; i < length; i++) vga_putchar(text[i]);
        return (int32_t)length;
    }
    resource = resource_for(task_current_process_id(), false);
    raw_fd = resource_fd(resource, fd);
    if (raw_fd < 0) return -BK_EBADF;
    return vfs_write(raw_fd, buffer, length);
}

static int32_t sys_close(uint32_t fd) {
    syscall_resources_t *resource =
        resource_for(task_current_process_id(), false);
    uint32_t slot;
    if (!resource || fd < 3U) return -BK_EBADF;
    slot = fd - 3U;
    if (slot >= SYSCALL_FD_SLOTS || resource->fds[slot] < 0)
        return -BK_EBADF;
    if (!vfs_close(resource->fds[slot])) return -BK_EIO;
    resource->fds[slot] = -1;
    return 0;
}

static int32_t sys_alloc(uint32_t size) {
    syscall_resources_t *resource;
    void *allocation;
    if (!size || size > SYSCALL_ALLOC_MAX) return -BK_EINVAL;
    resource = resource_for(task_current_process_id(), true);
    if (!resource) return -BK_ENOMEM;
    for (uint32_t i = 0; i < SYSCALL_ALLOC_SLOTS; i++) {
        if (resource->allocations[i]) continue;
        allocation = kzalloc(size);
        if (!allocation) return -BK_ENOMEM;
        resource->allocations[i] = allocation;
        return (int32_t)(uintptr_t)allocation;
    }
    return -BK_ENOMEM;
}

static int32_t sys_realloc(void *old_pointer, uint32_t size) {
    syscall_resources_t *resource =
        resource_for(task_current_process_id(), false);
    void *allocation;
    if (!old_pointer) return sys_alloc(size);
    if (!resource || !size || size > SYSCALL_ALLOC_MAX) return -BK_EINVAL;
    for (uint32_t i = 0; i < SYSCALL_ALLOC_SLOTS; i++) {
        if (resource->allocations[i] != old_pointer) continue;
        allocation = krealloc(old_pointer, size);
        if (!allocation) return -BK_ENOMEM;
        resource->allocations[i] = allocation;
        return (int32_t)(uintptr_t)allocation;
    }
    return -BK_EACCES;
}

static int32_t sys_free(void *pointer) {
    syscall_resources_t *resource =
        resource_for(task_current_process_id(), false);
    if (!pointer) return 0;
    if (!resource) return -BK_EACCES;
    for (uint32_t i = 0; i < SYSCALL_ALLOC_SLOTS; i++) {
        if (resource->allocations[i] != pointer) continue;
        kfree(pointer);
        resource->allocations[i] = NULL;
        return 0;
    }
    return -BK_EACCES;
}


static uint32_t syscall_domain_mask(uint32_t number) {
    switch (number) {
        case SYS_WRITE:
            return KDOMAIN_DRIVER;
        case SYS_OPEN:
        case SYS_READ:
        case SYS_CLOSE:
        case SYS_GETCWD:
        case SYS_CHDIR:
        case SYS_MKDIR:
        case SYS_UNLINK:
        case SYS_RENAME:
        case SYS_GETDENTS:
            return KDOMAIN_VFS;
        case SYS_ALLOC:
        case SYS_REALLOC:
        case SYS_FREE:
            return KDOMAIN_TASK;
        case SYS_SPAWN:
            return KDOMAIN_TASK | KDOMAIN_VFS | KDOMAIN_GUI;
        case SYS_WAITPID:
        case SYS_KILL:
        case SYS_THREAD_CREATE:
        case SYS_THREAD_JOIN:
        case SYS_THREAD_DETACH:
        case SYS_THREAD_EXIT:
        case SYS_THREAD_TLS_SET:
        case SYS_THREAD_TLS_GET:
            return KDOMAIN_TASK;
        case SYS_NET_RESOLVE:
        case SYS_NET_SOCKET:
        case SYS_NET_CONNECT:
        case SYS_NET_SEND:
        case SYS_NET_RECV:
        case SYS_NET_CLOSE:
        case SYS_NET_HTTP_GET:
            return KDOMAIN_NET;
        default:
            return 0U;
    }
}

registers_t *syscall_handler(registers_t *regs) {
    char path[VFS_MAX_PATH];
    char second_path[VFS_MAX_PATH];
    char thread_name[TASK_NAME_LEN];
    int32_t result;
    uint32_t domain_mask;

    if (!regs) return regs;
    if (regs->eax == SYS_WIN16_RELAY) {
        kernel_domains_enter(KDOMAIN_WINE | KDOMAIN_LEGACY);
        regs = ne_win16_syscall(regs);
        kernel_domains_exit(KDOMAIN_WINE | KDOMAIN_LEGACY);
        return regs;
    }
    if (regs->eax == SYS_UPCALL_RETURN) {
        kernel_domains_enter(KDOMAIN_GUI | KDOMAIN_WINE);
        /* WIN32_CHAIN_PENDING_UPCALLS */
        if (!task_finish_user_upcall(regs)) {
            regs->eax = (uint32_t)-BK_EINVAL;
        } else {
            (void)task_prepare_user_upcall(regs);
        }
        kernel_domains_exit(KDOMAIN_GUI | KDOMAIN_WINE);
        return regs;
    }
    if (!task_current_is_win16() &&
        (regs->eax == SYS_SLEEP || regs->eax == SYS_YIELD) &&
        task_prepare_user_upcall(regs)) return regs;
    domain_mask = syscall_domain_mask(regs->eax);
    kernel_domains_enter(domain_mask);
#define SYSCALL_RETURN(frame_value) do { \
        kernel_domains_exit(domain_mask); \
        return (frame_value); \
    } while (0)
    switch (regs->eax) {
        case SYS_EXIT:
            task_exit_from_interrupt((int32_t)regs->ebx);
            SYSCALL_RETURN(task_schedule(regs));
        case SYS_WRITE:
            regs->eax = (uint32_t)sys_write(regs, regs->ebx,
                (const void *)(uintptr_t)regs->ecx, regs->edx);
            break;
        case SYS_GETPID: regs->eax = task_current_process_id(); break;
        case SYS_GETPPID: regs->eax = task_current_parent_pid(); break;
        case SYS_YIELD:
            regs->eax = 0;
            SYSCALL_RETURN(task_schedule(regs));
        case SYS_SLEEP:
            task_sleep_from_interrupt(regs->ebx);
            SYSCALL_RETURN(task_schedule(regs));
        case SYS_UPTIME_MS: {
            uint32_t hz = pit_get_frequency_hz();
            regs->eax = hz ? (pit_get_ticks() * 1000U) / hz : 0;
            break;
        }
        case SYS_ABI_VERSION: regs->eax = SYSCALL_ABI_VERSION; break;
        case SYS_OPEN:
            regs->eax = (uint32_t)sys_open(regs,
                (const char *)(uintptr_t)regs->ebx, regs->ecx);
            break;
        case SYS_READ:
            regs->eax = (uint32_t)sys_read(regs, regs->ebx,
                (void *)(uintptr_t)regs->ecx, regs->edx);
            break;
        case SYS_CLOSE: regs->eax = (uint32_t)sys_close(regs->ebx); break;
        case SYS_GETCWD: {
            const char *cwd = vfs_getcwd();
            uint32_t length = (uint32_t)kstrlen(cwd) + 1U;
            if (regs->ecx < length || !user_write_range_ok(regs,
                    (void *)(uintptr_t)regs->ebx, length))
                regs->eax = (uint32_t)-BK_EFAULT;
            else {
                if (caller_is_user(regs) &&
                    !copy_to_user((void *)(uintptr_t)regs->ebx, cwd, length))
                    regs->eax = (uint32_t)-BK_EFAULT;
                else {
                    if (!caller_is_user(regs))
                        kmemcpy((void *)(uintptr_t)regs->ebx, cwd, length);
                    regs->eax = length - 1U;
                }
            }
            break;
        }
        case SYS_CHDIR:
        case SYS_MKDIR:
        case SYS_UNLINK:
            if (!copy_user_string(regs, (const char *)(uintptr_t)regs->ebx,
                                  path, sizeof(path))) {
                regs->eax = (uint32_t)-BK_EFAULT;
                break;
            }
            if (regs->eax == SYS_CHDIR) result = vfs_chdir(path) ? 0 : -BK_ENOENT;
            else if (regs->eax == SYS_MKDIR) result = vfs_mkdir(path) ? 0 : -BK_EIO;
            else result = vfs_remove(path) ? 0 : -BK_ENOENT;
            regs->eax = (uint32_t)result;
            break;
        case SYS_RENAME:
            if (!copy_user_string(regs, (const char *)(uintptr_t)regs->ebx,
                                  path, sizeof(path)) ||
                !copy_user_string(regs, (const char *)(uintptr_t)regs->ecx,
                                  second_path, sizeof(second_path)))
                regs->eax = (uint32_t)-BK_EFAULT;
            else regs->eax = vfs_rename(path, second_path) ? 0U
                                                           : (uint32_t)-BK_EIO;
            break;
        case SYS_GETDENTS:
            if (!copy_user_string(regs, (const char *)(uintptr_t)regs->ebx,
                                  path, sizeof(path)) ||
                regs->edx > VFS_MAX_DIR_ENTRIES ||
                !user_write_range_ok(regs, (void *)(uintptr_t)regs->ecx,
                                     regs->edx * sizeof(vfs_dir_entry_t)) ||
                !user_write_range_ok(regs, (void *)(uintptr_t)regs->esi,
                                     sizeof(uint32_t)))
                regs->eax = (uint32_t)-BK_EFAULT;
            else regs->eax = vfs_listdir(path,
                    (vfs_dir_entry_t *)(uintptr_t)regs->ecx, regs->edx,
                    (uint32_t *)(uintptr_t)regs->esi) ? 0U : (uint32_t)-BK_EIO;
            break;
        case SYS_ALLOC: regs->eax = (uint32_t)sys_alloc(regs->ebx); break;
        case SYS_REALLOC:
            regs->eax = (uint32_t)sys_realloc((void *)(uintptr_t)regs->ebx,
                                              regs->ecx);
            break;
        case SYS_FREE:
            regs->eax = (uint32_t)sys_free((void *)(uintptr_t)regs->ebx);
            break;
        case SYS_SPAWN:
            if (!copy_user_string(regs, (const char *)(uintptr_t)regs->ebx,
                                  path, sizeof(path)) ||
                (regs->ecx && !copy_user_string(regs,
                    (const char *)(uintptr_t)regs->ecx, second_path,
                    sizeof(second_path))))
                regs->eax = (uint32_t)-BK_EFAULT;
            else {
                int pid = elf_spawn_program_ex(path, gui_get_desktop(),
                                               regs->ecx ? second_path : NULL);
                regs->eax = pid >= 0 ? (uint32_t)pid : (uint32_t)-BK_ENOENT;
            }
            break;
        case SYS_WAITPID: {
            int32_t status = 0;
            if (regs->ecx && !user_write_range_ok(regs,
                    (void *)(uintptr_t)regs->ecx, sizeof(status))) {
                regs->eax = (uint32_t)-BK_EFAULT;
                break;
            }
            result = task_waitpid(regs->ebx, &status);
            if (result < 0) result = -BK_ECHILD;
            if (result > 0 && regs->ecx) {
                if (caller_is_user(regs)) {
                    if (!copy_to_user((void *)(uintptr_t)regs->ecx,
                                      &status, sizeof(status)))
                        result = -BK_EFAULT;
                } else {
                    *(int32_t *)(uintptr_t)regs->ecx = status;
                }
            }
            regs->eax = (uint32_t)result;
            break;
        }
        case SYS_KILL:
            regs->eax = task_request_exit(regs->ebx) ? 0U
                                                     : (uint32_t)-BK_ENOENT;
            break;
        case SYS_THREAD_CREATE: {
            const char *name = (const char *)(uintptr_t)regs->esi;
            task_entry_t entry = (task_entry_t)(uintptr_t)regs->ebx;
            if (!caller_is_user(regs) || !entry ||
                !user_range_ok(regs, (const void *)(uintptr_t)regs->ebx, 1U)) {
                regs->eax = (uint32_t)-BK_EFAULT;
                break;
            }
            if (name) {
                if (!copy_user_string(regs, name, thread_name,
                                      sizeof(thread_name))) {
                    regs->eax = (uint32_t)-BK_EFAULT;
                    break;
                }
            } else {
                kstrcpy(thread_name, "app-thread");
            }
            regs->eax = (uint32_t)task_create_user_thread_ex(
                thread_name, entry, (void *)(uintptr_t)regs->ecx,
                task_current_process_id(), regs->edx, true);
            break;
        }
        case SYS_THREAD_JOIN: {
            int32_t thread_status = 0;
            int32_t joined;
            /* Validate the destination before consuming the one-shot join
             * completion. Otherwise an invalid user pointer would lose the
             * result and make a second join fail permanently. */
            if (regs->ecx && !user_write_range_ok(regs,
                    (void *)(uintptr_t)regs->ecx, sizeof(thread_status))) {
                regs->eax = (uint32_t)-BK_EFAULT;
                break;
            }
            joined = task_thread_join_try(regs->ebx, &thread_status);
            if (joined > 0 && regs->ecx) {
                if (caller_is_user(regs)) {
                    if (!copy_to_user((void *)(uintptr_t)regs->ecx,
                                      &thread_status,
                                      sizeof(thread_status))) {
                        /* The mapping was checked before the join claim; a
                         * later failure means the process changed it during
                         * the syscall and is treated as EFAULT. */
                        regs->eax = (uint32_t)-BK_EFAULT;
                        break;
                    }
                } else {
                    *(int32_t *)(uintptr_t)regs->ecx = thread_status;
                }
            }
            regs->eax = (uint32_t)joined;
            break;
        }
        case SYS_THREAD_DETACH:
            regs->eax = task_thread_detach(regs->ebx) ? 0U :
                        (uint32_t)-BK_EINVAL;
            break;
        case SYS_THREAD_EXIT:
            task_exit_from_interrupt((int32_t)regs->ebx);
            SYSCALL_RETURN(task_schedule(regs));
        case SYS_THREAD_SELF:
            regs->eax = task_current_tid();
            break;
        case SYS_THREAD_TLS_SET:
            if (regs->ebx && !user_write_range_ok(regs,
                    (void *)(uintptr_t)regs->ebx, 4096U)) {
                regs->eax = (uint32_t)-BK_EFAULT;
                break;
            }
            if (!task_set_current_tls_base(regs->ebx)) {
                regs->eax = (uint32_t)-BK_EINVAL;
                break;
            }
            regs->fs = regs->ebx ? GDT_USER_FS : GDT_USER_DATA;
            regs->eax = 0U;
            break;
        case SYS_THREAD_TLS_GET:
            regs->eax = task_current_tls_base();
            break;
        case SYS_NET_RESOLVE:
            if (!copy_user_string(regs, (const char *)(uintptr_t)regs->ebx,
                                  path, sizeof(path)) ||
                !user_write_range_ok(regs, (void *)(uintptr_t)regs->ecx, 4U))
                regs->eax = (uint32_t)-BK_EFAULT;
            else regs->eax = network_resolve(path,
                    (uint8_t *)(uintptr_t)regs->ecx, regs->edx) ? 0U
                                                               : (uint32_t)-BK_EIO;
            break;
        case SYS_NET_SOCKET:
            regs->eax = (uint32_t)sys_net_socket(regs->ebx);
            break;
        case SYS_NET_CONNECT: {
            syscall_resources_t *resource =
                resource_for(task_current_process_id(), false);
            int raw_socket = resource_socket(resource, regs->ebx);
            if (raw_socket < 0) regs->eax = (uint32_t)-BK_EBADF;
            else if (!user_range_ok(regs,
                         (const void *)(uintptr_t)regs->ecx, 4U))
                regs->eax = (uint32_t)-BK_EFAULT;
            else regs->eax = network_socket_connect(raw_socket,
                    (const uint8_t *)(uintptr_t)regs->ecx,
                    (uint16_t)regs->edx, regs->esi) ? 0U
                                                   : (uint32_t)-BK_EIO;
            break;
        }
        case SYS_NET_SEND:
        case SYS_NET_RECV: {
            syscall_resources_t *resource =
                resource_for(task_current_process_id(), false);
            int raw_socket = resource_socket(resource, regs->ebx);
            if (raw_socket < 0) regs->eax = (uint32_t)-BK_EBADF;
            else if (regs->edx > SYSCALL_WRITE_MAX ||
                     (regs->eax == SYS_NET_SEND
                        ? !user_range_ok(regs,
                            (const void *)(uintptr_t)regs->ecx, regs->edx)
                        : !user_write_range_ok(regs,
                            (void *)(uintptr_t)regs->ecx, regs->edx)))
                regs->eax = (uint32_t)-BK_EFAULT;
            else if (regs->eax == SYS_NET_SEND)
                regs->eax = (uint32_t)network_socket_send(raw_socket,
                    (const void *)(uintptr_t)regs->ecx, regs->edx, regs->esi);
            else regs->eax = (uint32_t)network_socket_receive(raw_socket,
                    (void *)(uintptr_t)regs->ecx, regs->edx, regs->esi);
            break;
        }
        case SYS_NET_CLOSE:
            regs->eax = (uint32_t)sys_net_close(regs->ebx);
            break;
        case SYS_NET_HTTP_GET:
            if (!copy_user_string(regs, (const char *)(uintptr_t)regs->ebx,
                                  path, sizeof(path)) ||
                regs->edx > SYSCALL_WRITE_MAX ||
                !user_write_range_ok(regs, (void *)(uintptr_t)regs->ecx,
                                     regs->edx))
                regs->eax = (uint32_t)-BK_EFAULT;
            else if (kstrncmp(path, "https://", 8U) == 0)
                regs->eax = (uint32_t)network_https_get(path,
                    (void *)(uintptr_t)regs->ecx, regs->edx, regs->esi);
            else regs->eax = (uint32_t)network_http_get(path,
                    (void *)(uintptr_t)regs->ecx, regs->edx, regs->esi);
            break;
        case SYS_API_CALL: {
            uint32_t arguments[16];
            uint32_t argument_address = regs->useresp + sizeof(uint32_t);
            uint32_t stack_limit = 0U;
            uint32_t stack_base = 0U;
            uint32_t readable_bytes;
            uint64_t value;
            uint32_t callee_cleanup = 0;
            bool valid = false;

            /* API thunks have no signature metadata and the raw dispatcher
             * supplies up to 16 words. Requiring all 64 bytes to exist above
             * ESP rejects perfectly valid zero/one-argument calls near the
             * top of a guarded stack and returns -EFAULT (0xFFFFFFF2) to the
             * application, which then treats it as a pointer. Copy only the
             * words that really fit in this task's stack and zero-fill the
             * unused tail. */
            kmemset(arguments, 0, sizeof(arguments));
            if (!caller_is_user(regs) || regs->useresp < HEAP_START ||
                argument_address < regs->useresp ||
                !task_get_user_stack_bounds(task_current_pid(),
                                            &stack_limit, &stack_base) ||
                regs->useresp < stack_limit || regs->useresp >= stack_base ||
                argument_address < stack_limit || argument_address > stack_base) {
                regs->eax = (uint32_t)-BK_EFAULT;
                break;
            }
            readable_bytes = stack_base - argument_address;
            if (readable_bytes > sizeof(arguments))
                readable_bytes = sizeof(arguments);
            if (readable_bytes && !copy_from_user(
                    arguments, (const void *)(uintptr_t)argument_address,
                    readable_bytes)) {
                regs->eax = (uint32_t)-BK_EFAULT;
                break;
            }

            /* Public APIs execute on the task's kernel stack. Reject the call
             * before a deep renderer/GUI chain can overwrite the syscall
             * frame; the process is terminated, but the desktop survives. */
            {
                uint32_t remaining = task_kernel_stack_remaining();
                if (remaining != 0xFFFFFFFFU &&
                    remaining < SYS_API_KERNEL_STACK_RESERVE) {
                    kprintf("[API] pila kernel agotada pid=%u api_token=%u "
                            "restante=%u; proceso terminado\n",
                            task_current_pid(), regs->ecx, remaining);
                    task_exit_from_interrupt(0xA501);
                    task_allow_kernel_switch_once();
                    SYSCALL_RETURN(task_schedule(regs));
                }
            }
            {
                value = elf_user_api_dispatch(
                    regs->ecx, arguments, &valid, &callee_cleanup);

                if (!valid) {
                    regs->eax = (uint32_t)-BK_EACCES;
                    break;
                }

                regs->eax = (uint32_t)value;
                regs->edx = (uint32_t)(value >> 32);
            }
                        if (callee_cleanup) {
                /* WINE_SAFE_CALLEE_CLEANUP */
                /*
                 * elf_api_call_raw informa el ret N ejecutado por
                 * el wrapper stdcall. No aplicar N al ESP de Ring 3
                 * hasta validar firma, límites y dirección de retorno.
                 */
                uint32_t stack_limit = 0U;
                uint32_t stack_base = 0U;
                uint32_t new_useresp =
                    regs->useresp + sizeof(uint32_t) + callee_cleanup;
                uint32_t return_address = 0U;
                bool return_read_ok = caller_is_user(regs)
                    ? copy_from_user(&return_address,
                        (const void *)(uintptr_t)regs->useresp,
                        sizeof(return_address))
                    : true;
                if (!caller_is_user(regs))
                    return_address =
                        *(const uint32_t *)(uintptr_t)regs->useresp;
                bool cleanup_valid = return_read_ok &&
                    (callee_cleanup & 3U) == 0U &&
                    callee_cleanup <= SYS_API_MAX_CALLEE_CLEANUP &&
                    new_useresp >= regs->useresp &&
                    task_get_user_stack_bounds(
                        task_current_pid(), &stack_limit, &stack_base) &&
                    regs->useresp >= stack_limit &&
                    regs->useresp < stack_base &&
                    new_useresp >= stack_limit &&
                    new_useresp <= stack_base &&
                    syscall_win32_return_address_ok(return_address);

                if (!cleanup_valid) {
                    kprintf(
                        "[WIN32] ABI invalida en %s: cleanup=%u "
                        "ESP=%x nuevo=%x retorno=%x stack=%x..%x\n",
                        elf_last_user_api_name(), callee_cleanup,
                        regs->useresp, new_useresp, return_address,
                        stack_limit, stack_base);
                    task_exit_from_interrupt(0x570D);
                    SYSCALL_RETURN(task_schedule(regs));
                }

                regs->useresp = new_useresp;
                regs->eip = return_address;
            }
            /* Las apps nativas duermen dentro de una llamada API proxy. Al
             * volver de esa llamada es el punto seguro para entregar eventos
             * y callbacks pendientes conservando el valor de retorno. */
            if (!task_current_is_win16())
                (void)task_prepare_user_upcall(regs);
            break;
        }
        case SYS_WIN32_EXCEPTION_RETURN:
            if (!win32_exception_restore_context(regs))
                regs->eax = (uint32_t)-BK_EINVAL;
            break;
        default: regs->eax = (uint32_t)-BK_ENOSYS; break;
    }
    kernel_domains_exit(domain_mask);
#undef SYSCALL_RETURN
    return regs;
}
