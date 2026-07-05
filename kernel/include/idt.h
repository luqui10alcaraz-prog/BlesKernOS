#ifndef IDT_H
#define IDT_H

#include "types.h"

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} PACKED idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} PACKED idt_ptr_t;

/*
 * Frame que el stub ASM construye sobre el stack antes de llamar a C.
 * El orden debe coincidir con el que se empuja en isr_stubs.asm.
 */
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_no;
    uint32_t err_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t useresp;
    uint32_t ss;
} PACKED registers_t;

#define IDT_GATE_INTERRUPT 0x8E
#define IDT_GATE_TRAP      0x8F
#define IDT_GATE_USER_TRAP 0xEF

void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t handler, uint16_t selector, uint8_t flags);
registers_t *isr_handler(registers_t *regs);

extern void isr0(void); extern void isr1(void); extern void isr2(void);
extern void isr3(void); extern void isr4(void); extern void isr5(void);
extern void isr6(void); extern void isr7(void); extern void isr8(void);
extern void isr9(void); extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

extern void irq0(void); extern void irq1(void); extern void irq2(void);
extern void irq3(void); extern void irq4(void); extern void irq5(void);
extern void irq6(void); extern void irq7(void); extern void irq8(void);
extern void irq9(void); extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);
extern void isr128(void);

#endif
