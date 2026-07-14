#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"
#include "idt.h"

#define SYSCALL_VECTOR 0x80
#define SYSCALL_ABI_VERSION 2

#define BK_ENOENT  2
#define BK_EIO     5
#define BK_EBADF   9
#define BK_ECHILD 10
#define BK_ENOMEM 12
#define BK_EACCES 13
#define BK_EFAULT 14
#define BK_EBUSY  16
#define BK_EINVAL 22
#define BK_EMFILE 24
#define BK_ENOSYS 38

enum {
    SYS_EXIT = 0,
    SYS_WRITE,
    SYS_GETPID,
    SYS_YIELD,
    SYS_SLEEP,
    SYS_UPTIME_MS,
    SYS_ABI_VERSION,
    SYS_WIN32_EXCEPTION_RETURN,
    SYS_GETPPID,
    SYS_OPEN,
    SYS_READ,
    SYS_CLOSE,
    SYS_GETCWD,
    SYS_CHDIR,
    SYS_MKDIR,
    SYS_UNLINK,
    SYS_RENAME,
    SYS_GETDENTS,
    SYS_ALLOC,
    SYS_REALLOC,
    SYS_FREE,
    SYS_SPAWN,
    SYS_WAITPID,
    SYS_KILL,
    SYS_API_CALL,
    SYS_UPCALL_RETURN,
    SYS_COUNT
};

registers_t *syscall_handler(registers_t *regs);
void syscall_process_cleanup(uint32_t process_id);

static inline int32_t syscall0(uint32_t number) {
    int32_t result;
    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(number) : "memory");
    return result;
}

static inline int32_t syscall1(uint32_t number, uint32_t arg1) {
    int32_t result;
    __asm__ volatile ("int $0x80" : "=a"(result)
                      : "a"(number), "b"(arg1) : "memory");
    return result;
}

static inline int32_t syscall2(uint32_t number, uint32_t arg1,
                               uint32_t arg2) {
    int32_t result;
    __asm__ volatile ("int $0x80" : "=a"(result)
                      : "a"(number), "b"(arg1), "c"(arg2) : "memory");
    return result;
}

static inline int32_t syscall3(uint32_t number, uint32_t arg1,
                               uint32_t arg2, uint32_t arg3) {
    int32_t result;
    __asm__ volatile ("int $0x80" : "=a"(result)
                      : "a"(number), "b"(arg1), "c"(arg2), "d"(arg3)
                      : "memory");
    return result;
}


static inline int32_t syscall4(uint32_t number, uint32_t arg1,
                               uint32_t arg2, uint32_t arg3,
                               uint32_t arg4) {
    int32_t result;
    __asm__ volatile ("int $0x80" : "=a"(result)
                      : "a"(number), "b"(arg1), "c"(arg2), "d"(arg3),
                        "S"(arg4) : "memory");
    return result;
}

#endif
