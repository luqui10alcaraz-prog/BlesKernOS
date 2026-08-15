#ifndef SMP_H
#define SMP_H

#include "types.h"
#include "idt.h"

/* xAPIC usa IDs de 8 bits. Esta implementación conserva un límite razonable
 * para el kernel de 32 bits y admite hasta 16 procesadores lógicos. Cada AP
 * mantiene GDT/TSS, pila y estado de scheduler propios. */
#define SMP_MAX_CPUS 16U
#define SMP_INVALID_CPU 0xFFU
#define SMP_LAPIC_TIMER_VECTOR 0xF0U
#define SMP_RESCHEDULE_VECTOR  0xF1U
#define SMP_LAPIC_SPURIOUS_VECTOR 0xFFU

void smp_init(void);
void smp_start_scheduler(void);

bool smp_is_available(void);
bool smp_scheduler_started(void);
uint32_t smp_cpu_count(void);
uint32_t smp_online_cpu_count(void);
bool smp_cpu_online(uint32_t cpu_index);
uint32_t smp_cpu_index(void);
uint8_t smp_cpu_apic_id(uint32_t cpu_index);
bool smp_is_bsp(void);

/* Legacy entry/exit gate. Native syscalls and Local APIC timers no longer use
 * it; it remains as quarantine for exceptions, PIC IRQs and unclassified
 * compatibility paths while those drivers are converted to subsystem locks. */
void smp_kernel_enter(void);
/* Interrupt/syscall entry: waits for the BKL with IF masked, then restores
 * the entry IF state once this CPU owns the recursive lock. */
void smp_kernel_enter_interrupt(void);
/* Local APIC timer entry is intentionally non-blocking. Idle APs must not
 * form a thundering herd on the Big Kernel Lock and starve CPU0/GUI. */
bool smp_kernel_try_enter_timer(void);
void smp_kernel_exit_frame(registers_t *frame);
void smp_kernel_relax(void);
void smp_kernel_reacquire(void);
bool smp_kernel_locked_by_current_cpu(void);

/* Local APIC services used by the dedicated timer stub. */
registers_t *smp_lapic_timer_interrupt(registers_t *frame);
registers_t *smp_reschedule_interrupt(registers_t *frame);
registers_t *smp_lapic_spurious_interrupt(registers_t *frame);
void smp_lapic_eoi(void);
void smp_reschedule_cpu(uint32_t cpu_index);
void smp_reschedule_mask(uint32_t cpu_mask);

/* Called by the AP trampoline after protected mode and paging are active. */
void smp_ap_entry(uint32_t cpu_index) NORETURN;

#endif
