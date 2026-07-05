#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"
#include "idt.h"

#define SYSCALL_VECTOR 0x80
#define SYSCALL_ABI_VERSION 1

enum {
    SYS_EXIT = 0,
    SYS_WRITE,
    SYS_GETPID,
    SYS_YIELD,
    SYS_SLEEP,
    SYS_UPTIME_MS,
    SYS_ABI_VERSION,
    SYS_COUNT
};

registers_t *syscall_handler(registers_t *regs);

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

static inline int32_t syscall3(uint32_t number, uint32_t arg1,
                               uint32_t arg2, uint32_t arg3) {
    int32_t result;
    __asm__ volatile ("int $0x80" : "=a"(result)
                      : "a"(number), "b"(arg1), "c"(arg2), "d"(arg3)
                      : "memory");
    return result;
}

#endif
