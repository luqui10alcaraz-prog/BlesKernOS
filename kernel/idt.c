#include "include/types.h"
#include "include/idt.h"
#include "include/vga.h"
#include "include/panic.h"
#include "include/task.h"
#include "include/gdt.h"
#include "include/paging.h"
#include "include/smp.h"
#include "include/elf_loader.h"
#include "win32/exception.h"
#include "../gui/gui.h"

static idt_entry_t idt[256];
static idt_ptr_t idt_ptr;
static bool idt_runtime_handlers_ready;

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

/* Last-resort recovery for an old or malformed native Ring-3 frame.  Native
 * BEX/ELF code uses flat data selectors, so #GP(0) is restartable after
 * restoring them.  Normal context switches sanitize the frame before it is
 * stored; reaching this path therefore remains useful diagnostic evidence. */
static bool recover_kernel_api_segments(registers_t *regs) {
    const char *api_name = NULL;
    uint32_t target = 0U, token = 0U;
    uint32_t start = (uint32_t)(uintptr_t)elf_api_call_raw_after_target;
    uint32_t end = (uint32_t)(uintptr_t)elf_api_call_raw_end;
    bool bad_segments;

    if (!regs || regs->int_no != 13U || (regs->cs & 3U) != 0U ||
        !task_current_is_user() || regs->eip < start || regs->eip >= end ||
        !elf_user_api_fault_info(&api_name, &target, &token))
        return false;

    bad_segments = (regs->ds & 0xFFFFU) != GDT_KERNEL_DATA ||
                   (regs->es & 0xFFFFU) != GDT_KERNEL_DATA ||
                   (regs->fs & 0xFFFFU) != GDT_KERNEL_DATA ||
                   (regs->gs & 0xFFFFU) != GDT_KERNEL_DATA;
    if (!bad_segments) return false;

    kprintf("[API] #GP de segmento reparado pid=%u cpu=%u api=%s "
            "token=%u target=%x eip=%x ds=%x es=%x fs=%x gs=%x\n",
            task_current_pid(), task_current_cpu(),
            api_name ? api_name : "?", token, target, regs->eip,
            regs->ds, regs->es, regs->fs, regs->gs);
    regs->ds = GDT_KERNEL_DATA;
    regs->es = GDT_KERNEL_DATA;
    regs->fs = GDT_KERNEL_DATA;
    regs->gs = GDT_KERNEL_DATA;
    return true;
}

static bool recover_flat_user_segments(registers_t *regs) {
    uint32_t old_ds, old_es, old_fs, old_gs;

    if (!regs || regs->int_no != 13U || regs->err_code != 0U ||
        (regs->cs & 0xFFFFU) != GDT_USER_CODE ||
        (regs->ds != 0U && regs->es != 0U && regs->fs != 0U &&
         regs->gs != 0U))
        return false;

    old_ds = regs->ds;
    old_es = regs->es;
    old_fs = regs->fs;
    old_gs = regs->gs;
    regs->ds = GDT_USER_DATA;
    regs->es = GDT_USER_DATA;
    regs->gs = GDT_USER_DATA;
    regs->fs = old_fs == GDT_USER_FS ? GDT_USER_FS : GDT_USER_DATA;
    kprintf("[TASK] #GP segmentos Ring 3 reparados pid=%u eip=%x "
            "ds=%x es=%x fs=%x gs=%x\n",
            task_current_pid(), regs->eip, old_ds, old_es, old_fs, old_gs);
    return true;
}

void idt_set_gate(uint8_t num, uint32_t handler, uint16_t selector, uint8_t flags) {
    idt[num].offset_low = (uint16_t)(handler & 0xFFFF);
    idt[num].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[num].selector = selector;
    idt[num].zero = 0;
    idt[num].type_attr = flags;
}

void idt_load_current(void) {
    __asm__ volatile ("lidt %0" : : "m"(idt_ptr));
}

void idt_init(void) {
    idt_runtime_handlers_ready = false;
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

    idt_set_gate(SMP_LAPIC_TIMER_VECTOR, (uint32_t)lapic_timer_stub,
                 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(SMP_RESCHEDULE_VECTOR, (uint32_t)lapic_reschedule_stub,
                 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(SMP_LAPIC_SPURIOUS_VECTOR, (uint32_t)lapic_spurious_stub,
                 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(0x80, (uint32_t)isr128, 0x08, IDT_GATE_USER_TRAP);
    idt_load_current();
}

void idt_enable_runtime_handlers(void) {
    idt_runtime_handlers_ready = true;
}

registers_t *isr_handler(registers_t *regs) {
    if (!regs) {
        kprintf("[EXC] Registro de interrupcion nulo.\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }

    /* Antes de task_init/paging completo no se puede entrar al panic grafico,
     * consultar la tarea actual ni intentar planificar: cualquiera de esas
     * rutas puede generar una segunda excepcion y recrear el triple fault que
     * precisamente queremos contener en hardware antiguo. */
    if (!idt_runtime_handlers_ready) {
        uint32_t fault_address = 0U;
        if (regs->int_no == 14U)
            __asm__ volatile ("movl %%cr2,%0" : "=r"(fault_address));
        __asm__ volatile ("outb %0,$0x80" : :
                          "a"((uint8_t)(0xE0U |
                                        (regs->int_no & 0x0FU))));
        kprintf("[EARLY EXC] int=%u error=%x eip=%x cr2=%x\n",
                regs->int_no, regs->err_code, regs->eip, fault_address);
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }

    if (regs->int_no < 32) {
        if (regs->int_no == 14U) {
            uint32_t fault_address = paging_fault_address();
            /* Another CPU may have published a permission change while this
             * CPU still had the old identity-map translation cached. Refresh
             * once before treating the access as a genuine fault. */
            if (paging_recover_stale_fault(fault_address)) return regs;
            kprintf("[PAGING] #PF addr=%x error=%x eip=%x cpl=%u\n",
                    fault_address, regs->err_code, regs->eip,
                    regs->cs & 3U);
        }
        if ((regs->cs & 3U) == 3U) {
            uint32_t image_base=0U,image_offset=0U;
            uint32_t process_id=task_current_process_id();
            const char *thunk_name = NULL;
            uint32_t thunk_target = 0U, thunk_token = 0U;
            if (recover_flat_user_segments(regs)) return regs;
            if (win32_exception_handle_interrupt(regs)) return regs;
            kprintf("[TASK] excepcion Ring 3 int=%u error=%x eip=%x "
                    "pid=%u ds=%x es=%x fs=%x gs=%x\n",
                    regs->int_no, regs->err_code, regs->eip,
                    task_current_pid(), regs->ds, regs->es, regs->fs,
                    regs->gs);
            kprintf("[TASK] regs eax=%x ebx=%x ecx=%x edx=%x esi=%x edi=%x "
                    "ebp=%x useresp=%x\n", regs->eax, regs->ebx, regs->ecx,
                    regs->edx, regs->esi, regs->edi, regs->ebp,
                    regs->useresp);
            if (elf_user_api_thunk_info(regs->eip, &thunk_name,
                                        &thunk_target, &thunk_token)) {
                kprintf("[API] fallo junto a thunk token=%u api=%s target=%x\n",
                        thunk_token, thunk_name ? thunk_name : "?",
                        thunk_target);
            } else if(elf_process_address(process_id,regs->eip,
                                   &image_base,&image_offset))
                kprintf("[ELF] fallo process=%u image=%x offset=%x\n",
                        process_id,image_base,image_offset);
            gui_desktop_show_alert(gui_get_desktop(), GUI_ALERT_ERROR,
                                   "El programa dejo de responder",
                                   "BlesKernOS cerro el programa porque produjo una excepcion de memoria o una instruccion invalida.",
                                   (int32_t)regs->int_no);
            task_exit_from_interrupt(128 + (int32_t)regs->int_no);
            return task_schedule(regs);
        }
        if (recover_unrequested_kernel_single_step(regs)) return regs;
        if (recover_kernel_api_segments(regs)) return regs;

        /* A bad pointer/segment supplied through a public API must not take
         * down the entire desktop. Limit containment to faults raised while a
         * native Ring-3 task is inside its guarded API proxy. The task is
         * discarded together with its kernel stack; unrelated kernel faults
         * still reach the normal panic screen. */
        if ((regs->int_no == 13U || regs->int_no == 14U) &&
            task_current_is_user()) {
            const char *api_name = NULL;
            uint32_t target = 0U, token = 0U;
            if (elf_user_api_fault_info(&api_name, &target, &token)) {
                uint32_t fault_address = regs->int_no == 14U
                    ? paging_fault_address() : 0U;
                kprintf("[API] fallo contenido pid=%u cpu=%u api=%s "
                        "token=%u target=%x int=%u error=%x eip=%x addr=%x\n",
                        task_current_pid(), task_current_cpu(),
                        api_name ? api_name : "?", token, target,
                        regs->int_no, regs->err_code, regs->eip,
                        fault_address);
                elf_user_api_fault_clear();
                task_exit_from_interrupt((int32_t)(0xA500U + regs->int_no));
                task_allow_kernel_switch_once();
                return task_schedule(regs);
            }
        }

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
