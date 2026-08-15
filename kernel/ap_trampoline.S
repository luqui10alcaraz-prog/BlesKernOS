/*
 * x86 application-processor startup trampoline.
 *
 * The BSP copies this position-independent block to physical 0x6000 and sends
 * SIPI vector 0x06.  All absolute data references point at a fixed mailbox in
 * the same low-memory page; labels used by branches are encoded as offsets
 * relative to ap_trampoline_start so the copied code does not need relocation.
 */

.section .ap_trampoline,"ax",@progbits
.code16
.global ap_trampoline_start
.global ap_trampoline_end

.set AP_TRAMPOLINE_PHYS, 0x6000
.set AP_GDT_DESC_PHYS,   0x6ee0
.set AP_MAILBOX_PHYS,    0x6f00
.set AP_MB_CR3,          0
.set AP_MB_STACK,        4
.set AP_MB_ENTRY,        8
.set AP_MB_CPU,          12

ap_trampoline_start:
    cli
    cld
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x5ff0, %sp

    lgdt AP_GDT_DESC_PHYS

    movl %cr0, %eax
    orl $0x00000001, %eax
    movl %eax, %cr0

    /* GAS needs the operand-size override for a 32-bit protected-mode target
     * while assembling in .code16.  The offset is a pure assembly-time delta. */
    ljmpl $0x08, $(AP_TRAMPOLINE_PHYS + ap_protected - ap_trampoline_start)

.code32
ap_protected:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    movl AP_MAILBOX_PHYS + AP_MB_STACK, %esp
    xorl %ebp, %ebp

    movl AP_MAILBOX_PHYS + AP_MB_CR3, %eax
    movl %eax, %cr3
    movl %cr4, %eax
    orl $0x00000010, %eax             /* CR4.PSE */
    movl %eax, %cr4
    movl %cr0, %eax
    orl $0x80010000, %eax             /* CR0.PG | CR0.WP */
    movl %eax, %cr0
    jmp 1f
1:
    pushl AP_MAILBOX_PHYS + AP_MB_CPU
    movl AP_MAILBOX_PHYS + AP_MB_ENTRY, %eax
    call *%eax

2:
    cli
    hlt
    jmp 2b

ap_trampoline_end:

/* El trampoline no requiere una pila ELF ejecutable. */
.section .note.GNU-stack,"",@progbits
