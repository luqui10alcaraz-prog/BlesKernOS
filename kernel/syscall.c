#include "include/syscall.h"
#include "include/task.h"
#include "include/pit.h"
#include "include/vga.h"
#include "include/vfs.h"
#include "include/memory.h"
#include "include/elf_loader.h"
#include "../gui/gui.h"
#include "win32/exception.h"

#define SYSCALL_WRITE_MAX       65536U
#define SYSCALL_ALLOC_MAX       (8U * 1024U * 1024U)
#define SYSCALL_RESOURCE_SLOTS  16
#define SYSCALL_FD_SLOTS        VFS_MAX_OPEN_FILES
#define SYSCALL_ALLOC_SLOTS     32

typedef struct {
    uint32_t process_id;
    int fds[SYSCALL_FD_SLOTS];
    void *allocations[SYSCALL_ALLOC_SLOTS];
} syscall_resources_t;

static syscall_resources_t resources[SYSCALL_RESOURCE_SLOTS];

static bool caller_is_user(const registers_t *regs) {
    return regs && (regs->cs & 3U) == 3U;
}

/*
 * Transitional Phase-II user range. Native ELF/PE images, their stacks and
 * syscall allocations all live in the managed heap. Rejecting low memory
 * prevents an application from making the kernel dereference its IDT, GDT or
 * kernel image. Paging can later replace this check without changing the ABI.
 */
static bool user_range_ok(const registers_t *regs, const void *pointer,
                          uint32_t length) {
    uint32_t start = (uint32_t)(uintptr_t)pointer;
    uint32_t end;

    if (!caller_is_user(regs)) return pointer != NULL || length == 0U;
    if (length == 0U) return start >= HEAP_START && start <= HEAP_END;
    if (!pointer || start < HEAP_START || start >= HEAP_END) return false;
    end = start + length;
    return end >= start && end <= HEAP_END;
}

static bool copy_user_string(const registers_t *regs, const char *source,
                             char *destination, uint32_t capacity) {
    if (!source || !destination || capacity < 2U) return false;
    for (uint32_t i = 0; i < capacity; i++) {
        if (!user_range_ok(regs, source + i, 1U)) return false;
        destination[i] = source[i];
        if (destination[i] == '\0') return true;
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
    kmemset(resource, 0, sizeof(*resource));
}

static int resource_fd(syscall_resources_t *resource, uint32_t user_fd) {
    uint32_t slot;
    if (!resource || user_fd < 3U) return -1;
    slot = user_fd - 3U;
    return slot < SYSCALL_FD_SLOTS ? resource->fds[slot] : -1;
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
    if (length > SYSCALL_WRITE_MAX || !user_range_ok(regs, buffer, length))
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

registers_t *syscall_handler(registers_t *regs) {
    char path[VFS_MAX_PATH];
    char second_path[VFS_MAX_PATH];
    int32_t result;

    if (!regs) return regs;
    if (regs->eax == SYS_UPCALL_RETURN) {
        if (!task_finish_user_upcall(regs)) regs->eax = (uint32_t)-BK_EINVAL;
        return regs;
    }
    if ((regs->eax == SYS_SLEEP || regs->eax == SYS_YIELD) &&
        task_prepare_user_upcall(regs)) return regs;
    switch (regs->eax) {
        case SYS_EXIT:
            task_exit_from_interrupt((int32_t)regs->ebx);
            return task_schedule(regs);
        case SYS_WRITE:
            regs->eax = (uint32_t)sys_write(regs, regs->ebx,
                (const void *)(uintptr_t)regs->ecx, regs->edx);
            break;
        case SYS_GETPID: regs->eax = task_current_pid(); break;
        case SYS_GETPPID: regs->eax = task_current_parent_pid(); break;
        case SYS_YIELD:
            regs->eax = 0;
            return task_schedule(regs);
        case SYS_SLEEP:
            task_sleep_from_interrupt(regs->ebx);
            return task_schedule(regs);
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
            if (regs->ecx < length || !user_range_ok(regs,
                    (void *)(uintptr_t)regs->ebx, length))
                regs->eax = (uint32_t)-BK_EFAULT;
            else {
                kmemcpy((void *)(uintptr_t)regs->ebx, cwd, length);
                regs->eax = length - 1U;
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
                !user_range_ok(regs, (void *)(uintptr_t)regs->ecx,
                               regs->edx * sizeof(vfs_dir_entry_t)) ||
                !user_range_ok(regs, (void *)(uintptr_t)regs->esi,
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
            if (regs->ecx && !user_range_ok(regs,
                    (void *)(uintptr_t)regs->ecx, sizeof(status))) {
                regs->eax = (uint32_t)-BK_EFAULT;
                break;
            }
            result = task_waitpid(regs->ebx, &status);
            if (result < 0) result = -BK_ECHILD;
            if (result > 0 && regs->ecx)
                *(int32_t *)(uintptr_t)regs->ecx = status;
            regs->eax = (uint32_t)result;
            break;
        }
        case SYS_KILL:
            regs->eax = task_request_exit(regs->ebx) ? 0U
                                                     : (uint32_t)-BK_ENOENT;
            break;
        case SYS_API_CALL: {
            uint32_t arguments[16];
            uint32_t argument_address = regs->useresp + sizeof(uint32_t);
            uint64_t value;
            uint32_t callee_cleanup = 0;
            bool valid = false;
            if (!caller_is_user(regs) || regs->useresp < HEAP_START ||
                !user_range_ok(regs, (const void *)(uintptr_t)argument_address,
                               sizeof(arguments))) {
                regs->eax = (uint32_t)-BK_EFAULT;
                break;
            }
            kmemcpy(arguments, (const void *)(uintptr_t)argument_address,
                    sizeof(arguments));
            value = elf_user_api_dispatch(regs->ecx, arguments, &valid,
                                          &callee_cleanup);
            if (!valid) {
                regs->eax = (uint32_t)-BK_EACCES;
                break;
            }
            regs->eax = (uint32_t)value;
            regs->edx = (uint32_t)(value >> 32);
            if (callee_cleanup) {
                uint32_t return_address =
                    *(const uint32_t *)(uintptr_t)regs->useresp;
                regs->useresp += sizeof(uint32_t) + callee_cleanup;
                regs->eip = return_address;
            }
            /* Las apps nativas duermen dentro de una llamada API proxy. Al
             * volver de esa llamada es el punto seguro para entregar eventos
             * y callbacks pendientes conservando el valor de retorno. */
            (void)task_prepare_user_upcall(regs);
            break;
        }
        case SYS_WIN32_EXCEPTION_RETURN:
            if (!win32_exception_restore_context(regs))
                regs->eax = (uint32_t)-BK_EINVAL;
            break;
        default: regs->eax = (uint32_t)-BK_ENOSYS; break;
    }
    return regs;
}
