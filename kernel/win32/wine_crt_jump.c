/*
 * BlesKernOS Win32 CRT non-local jump support for i386.
 *
 * The register layout and restore order follow Wine's i386 __wine_longjmp
 * implementation and the Microsoft 32-bit _JUMP_BUFFER ABI.
 *
 * BlesKernOS does not yet implement the full RtlUnwind/SEH cleanup path.
 * Restoring FS:[0] removes inner exception frames from the active chain before
 * execution resumes at the context saved by _setjmp/_setjmp3.
 *
 * BLES_WINE_CRTDLL_LONGJMP_FIX_20260723
 */
#include "win32.h"
#include "../include/types.h"

typedef struct {
    uint32_t ebp;
    uint32_t ebx;
    uint32_t edi;
    uint32_t esi;
    uint32_t esp;
    uint32_t eip;
    uint32_t registration;
    uint32_t try_level;
    uint32_t cookie;
    uint32_t unwind_function;
    uint32_t unwind_data[6];
} win32_crt_jump_buffer32_t;

typedef char win32_crt_jump_buffer_must_be_64_bytes[
    sizeof(win32_crt_jump_buffer32_t) == 64U ? 1 : -1];

void win32_crt_longjmp(win32_crt_jump_buffer32_t *buffer,
                       int32_t value) __attribute__((noreturn));

__asm__(
    ".text\n"
    ".align 4\n"
    ".globl win32_crt_longjmp\n"
    ".type win32_crt_longjmp,@function\n"
    "win32_crt_longjmp:\n\t"
    "movl 4(%esp),%ecx\n\t"
    "movl 8(%esp),%eax\n\t"
    "testl %eax,%eax\n\t"
    "jnz 1f\n\t"
    "incl %eax\n"
    "1:\n\t"
    "movl 24(%ecx),%edx\n\t"
    "movl %edx,%fs:0\n\t"
    "movl 0(%ecx),%ebp\n\t"
    "movl 4(%ecx),%ebx\n\t"
    "movl 8(%ecx),%edi\n\t"
    "movl 12(%ecx),%esi\n\t"
    "movl 16(%ecx),%esp\n\t"
    "addl $4,%esp\n\t"
    "jmp *20(%ecx)\n\t"
    ".size win32_crt_longjmp,.-win32_crt_longjmp\n"
);
