#include "include/types.h"
#include "include/idt.h"
#include "include/vga.h"
#include "include/panic.h"
#include "include/task.h"
#include "win32/exception.h"

static idt_entry_t idt[256];
static idt_ptr_t idt_ptr;

static const char *exception_names[] = {
    "Divide Error (#DE)",
    "Debug (#DB)",
    "NMI Interrupt",
    "Breakpoint (#BP)",
    "Overflow (#OF)",
    "BOUND Range Exceeded (#BR)",
    "Invalid Opcode (#UD)",
    "Device Not Available (#NM)",
    "Double Fault (#DF)",
    "Coprocessor Segment Overrun",
    "Invalid TSS (#TS)",
    "Segment Not Present (#NP)",
    "Stack-Segment Fault (#SS)",
    "General Protection Fault (#GP)",
    "Page Fault (#PF)",
    "Reserved",
    "x87 FPU Error (#MF)",
    "Alignment Check (#AC)",
    "Machine Check (#MC)",
    "SIMD FP Exception (#XM)",
    "Virtualization Exception (#VE)",
    "Reserved"
};

static uint32_t exception_handlers[32] = {
    (uint32_t)isr0, (uint32_t)isr1, (uint32_t)isr2,  (uint32_t)isr3,
    (uint32_t)isr4, (uint32_t)isr5, (uint32_t)isr6,  (uint32_t)isr7,
    (uint32_t)isr8, (uint32_t)isr9, (uint32_t)isr10, (uint32_t)isr11,
    (uint32_t)isr12, (uint32_t)isr13, (uint32_t)isr14, (uint32_t)isr15,
    (uint32_t)isr16, (uint32_t)isr17, (uint32_t)isr18, (uint32_t)isr19,
    (uint32_t)isr20, (uint32_t)isr21, (uint32_t)isr22, (uint32_t)isr23,
    (uint32_t)isr24, (uint32_t)isr25, (uint32_t)isr26, (uint32_t)isr27,
    (uint32_t)isr28, (uint32_t)isr29, (uint32_t)isr30, (uint32_t)isr31,
};


#define X86_EFLAGS_TF (1U << 8)
#define X86_DR6_BREAKPOINT_MASK 0x0000000FU
#define X86_DR6_SINGLE_STEP (1U << 14)

static bool recover_unrequested_kernel_single_step(registers_t *regs) {
    uint32_t dr6;
    uint32_t clear = 0U;

    if (!regs || regs->int_no != 1U || (regs->cs & 3U) != 0U) return false;
    __asm__ volatile ("movl %%dr6, %0" : "=r"(dr6));

    /* Un breakpoint de hardware real debe seguir llegando al panic/debugger. */
    if (dr6 & X86_DR6_BREAKPOINT_MASK) return false;
    if ((dr6 & X86_DR6_SINGLE_STEP) == 0U &&
        (regs->eflags & X86_EFLAGS_TF) == 0U) return false;

    regs->eflags &= ~X86_EFLAGS_TF;
    __asm__ volatile ("movl %0, %%dr6" : : "r"(clear));
    kprintf("[EXC] #DB single-step no solicitado en kernel EIP=%x DR6=%x; TF limpiado\n",
            regs->eip, dr6);
    return true;
}

static uint32_t irq_handlers[16] = {
    (uint32_t)irq0, (uint32_t)irq1, (uint32_t)irq2,  (uint32_t)irq3,
    (uint32_t)irq4, (uint32_t)irq5, (uint32_t)irq6,  (uint32_t)irq7,
    (uint32_t)irq8, (uint32_t)irq9, (uint32_t)irq10, (uint32_t)irq11,
    (uint32_t)irq12, (uint32_t)irq13, (uint32_t)irq14, (uint32_t)irq15,
};

void idt_set_gate(uint8_t num, uint32_t handler, uint16_t selector, uint8_t flags) {
    idt[num].offset_low = (uint16_t)(handler & 0xFFFF);
    idt[num].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[num].selector = selector;
    idt[num].zero = 0;
    idt[num].type_attr = flags;
}

static void idt_load(void) {
    __asm__ volatile ("lidt %0" : : "m"(idt_ptr));
}

void idt_init(void) {
    idt_ptr.limit = (uint16_t)(sizeof(idt) - 1);
    idt_ptr.base = (uint32_t)&idt;

    for (int i = 0; i < 256; i++) {
        idt_set_gate((uint8_t)i, 0, 0, 0);
    }

    for (int i = 0; i < 32; i++) {
        idt_set_gate((uint8_t)i, exception_handlers[i], 0x08, IDT_GATE_INTERRUPT);
    }

    for (int i = 0; i < 16; i++) {
        idt_set_gate((uint8_t)(32 + i), irq_handlers[i], 0x08, IDT_GATE_INTERRUPT);
    }

    idt_set_gate(0x80, (uint32_t)isr128, 0x08, IDT_GATE_USER_TRAP);
    idt_load();
}

registers_t *isr_handler(registers_t *regs) {
    if (!regs) {
        kprintf("[EXC] Registro de interrupcion nulo.\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }

    if (regs->int_no < 32) {
        if ((regs->cs & 3U) == 3U) {
            if (win32_exception_handle_interrupt(regs)) return regs;
            task_exit_from_interrupt(128 + (int32_t)regs->int_no);
            return task_schedule(regs);
        }
        if (recover_unrequested_kernel_single_step(regs)) return regs;
        uint32_t names = sizeof(exception_names) / sizeof(exception_names[0]);
        const char *name = regs->int_no < names
            ? exception_names[regs->int_no] : "Reserved exception";
        panic_show(name, regs->int_no, regs->err_code, regs->eip);
    } else {
        kprintf("[IRQ] IRQ %u\n", regs->int_no - 32);
    }

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
