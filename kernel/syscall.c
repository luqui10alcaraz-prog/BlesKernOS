#include "include/syscall.h"
#include "include/task.h"
#include "include/pit.h"
#include "include/vga.h"

#define ERR_ENOSYS 38
#define ERR_EINVAL 22
#define ERR_EFAULT 14
#define SYSCALL_WRITE_MAX 4096U

static int32_t sys_write(uint32_t fd, const char *buffer, uint32_t length) {
    if (fd != 1 && fd != 2) return -ERR_EINVAL;
    if (!buffer || length > SYSCALL_WRITE_MAX ||
        (uint32_t)(uintptr_t)buffer + length < (uint32_t)(uintptr_t)buffer)
        return -ERR_EFAULT;
    for (uint32_t i = 0; i < length; i++) vga_putchar(buffer[i]);
    return (int32_t)length;
}

registers_t *syscall_handler(registers_t *regs) {
    uint32_t number;
    if (!regs) return regs;

    number = regs->eax;
    switch (number) {
        case SYS_EXIT:
            task_exit_from_interrupt();
            return task_schedule(regs);
        case SYS_WRITE:
            regs->eax = (uint32_t)sys_write(regs->ebx,
                                            (const char *)(uintptr_t)regs->ecx,
                                            regs->edx);
            break;
        case SYS_GETPID:
            regs->eax = task_current_pid();
            break;
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
        case SYS_ABI_VERSION:
            regs->eax = SYSCALL_ABI_VERSION;
            break;
        default:
            regs->eax = (uint32_t)-ERR_ENOSYS;
            break;
    }
    return regs;
}
