#include "include/smp.h"
#include "include/memory.h"
#include "include/paging.h"
#include "include/pic.h"
#include "include/pit.h"
#include "include/gdt.h"
#include "include/idt.h"
#include "include/task.h"
#include "include/vga.h"

#define IA32_APIC_BASE_MSR       0x1BU
#define IA32_APIC_BASE_ENABLE    (1U << 11)
#define IA32_APIC_BASE_BSP       (1U << 8)
#define CPUID_EDX_APIC           (1U << 9)

#define LAPIC_ID                 0x020U
#define LAPIC_TPR                0x080U
#define LAPIC_EOI                0x0B0U
#define LAPIC_SVR                0x0F0U
#define LAPIC_ESR                0x280U
#define LAPIC_ICR_LOW            0x300U
#define LAPIC_ICR_HIGH           0x310U
#define LAPIC_LVT_TIMER          0x320U
#define LAPIC_TIMER_INITIAL      0x380U
#define LAPIC_TIMER_CURRENT      0x390U
#define LAPIC_TIMER_DIVIDE       0x3E0U

#define LAPIC_SVR_ENABLE         (1U << 8)
#define LAPIC_LVT_MASKED         (1U << 16)
#define LAPIC_TIMER_PERIODIC     (1U << 17)
#define LAPIC_ICR_DELIVERY       (1U << 12)
#define LAPIC_ICR_LEVEL_ASSERT   (1U << 14)
#define LAPIC_ICR_TRIGGER_LEVEL  (1U << 15)
#define LAPIC_DM_INIT            (5U << 8)
#define LAPIC_DM_STARTUP         (6U << 8)

#define AP_TRAMPOLINE_PHYS       0x00006000U
#define AP_TRAMPOLINE_LIMIT      0x00006E00U
#define AP_GDT_PHYS              0x00006E00U
#define AP_GDT_DESC_PHYS         0x00006EE0U
#define AP_MAILBOX_PHYS          0x00006F00U
#define AP_STACK_SIZE            32768U
#define AP_STARTUP_VECTOR        (AP_TRAMPOLINE_PHYS >> 12U)

#define RSDP_SIGNATURE_LOW       0x20445352U /* "RSD " */
#define RSDP_SIGNATURE_HIGH      0x20525450U /* "PTR " */
#define ACPI_SIG_RSDT            0x54445352U /* RSDT */
#define ACPI_SIG_APIC            0x43495041U /* APIC */
#define MP_FLOAT_SIGNATURE       0x5F504D5FU /* _MP_ */
#define MP_CONFIG_SIGNATURE      0x504D4350U /* PCMP */

#define SMP_BOOT_TIMEOUT_MS      1000U
#define SMP_TIMER_CAL_TICKS      12U

extern uint8_t ap_trampoline_start[];
extern uint8_t ap_trampoline_end[];

typedef struct {
    uint8_t apic_id;
    uint8_t present;
    volatile uint8_t online;
    uint8_t bsp;
    uint32_t *stack;
    void *stack_allocation;
    uint32_t stack_size;
} smp_cpu_desc_t;

typedef struct {
    uint32_t cr3;
    uint32_t stack_top;
    uint32_t entry;
    uint32_t cpu_index;
    volatile uint32_t acknowledged;
} PACKED ap_mailbox_t;

typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} PACKED acpi_rsdp_t;

typedef struct {
    uint32_t signature;
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} PACKED acpi_sdt_header_t;

typedef struct {
    acpi_sdt_header_t header;
    uint32_t lapic_address;
    uint32_t flags;
} PACKED acpi_madt_t;

typedef struct {
    uint32_t signature;
    uint32_t config_table;
    uint8_t length_units;
    uint8_t specification_revision;
    uint8_t checksum;
    uint8_t feature[5];
} PACKED mp_floating_t;

typedef struct {
    uint32_t signature;
    uint16_t base_length;
    uint8_t specification_revision;
    uint8_t checksum;
    char oem_id[8];
    char product_id[12];
    uint32_t oem_table;
    uint16_t oem_table_size;
    uint16_t entry_count;
    uint32_t lapic_address;
    uint16_t extended_length;
    uint8_t extended_checksum;
    uint8_t reserved;
} PACKED mp_config_t;

typedef struct {
    uint8_t type;
    uint8_t apic_id;
    uint8_t apic_version;
    uint8_t flags;
    uint32_t cpu_signature;
    uint32_t feature_flags;
    uint32_t reserved[2];
} PACKED mp_processor_t;

static smp_cpu_desc_t g_cpus[SMP_MAX_CPUS];
static uint8_t g_apic_to_cpu[256];
static uint32_t g_cpu_count = 1U;
static volatile uint32_t g_online_count = 1U;
static volatile uint32_t g_release_aps;
static volatile uint32_t g_scheduler_started;
static bool g_smp_available;

static volatile uint32_t g_kernel_lock;
static volatile uint32_t g_bsp_waiting;
static volatile uint8_t g_kernel_owner = SMP_INVALID_CPU;
static uint8_t g_kernel_depth[SMP_MAX_CPUS];

static volatile uint32_t *g_lapic;
static uint32_t g_lapic_phys = 0xFEE00000U;
static uint32_t g_lapic_timer_initial = 100000U;

static uint8_t checksum8(const void *data, uint32_t length) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint8_t sum = 0U;
    while (length--) sum = (uint8_t)(sum + *bytes++);
    return sum;
}

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b,
                  uint32_t *c, uint32_t *d) {
    uint32_t eax = leaf, ebx, ecx, edx;
    __asm__ volatile ("cpuid"
                      : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (a) *a = eax;
    if (b) *b = ebx;
    if (c) *c = ecx;
    if (d) *d = edx;
}

static uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32U) | low;
}

static void wrmsr(uint32_t msr, uint64_t value) {
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"((uint32_t)value),
                      "d"((uint32_t)(value >> 32U)) : "memory");
}

static uint32_t lapic_read(uint32_t reg) {
    if (!g_lapic) return 0U;
    return g_lapic[reg >> 2U];
}

static void lapic_write(uint32_t reg, uint32_t value) {
    if (!g_lapic) return;
    g_lapic[reg >> 2U] = value;
    (void)g_lapic[LAPIC_ID >> 2U];
}

static void lapic_wait_delivery(void) {
    uint32_t guard = 1000000U;
    while ((lapic_read(LAPIC_ICR_LOW) & LAPIC_ICR_DELIVERY) && guard--)
        __asm__ volatile ("pause");
}

static void smp_short_delay(uint32_t loops) {
    while (loops--) io_wait();
}

static uint32_t smp_save_flags(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0" : "=r"(flags));
    return flags;
}

static void smp_restore_flags(uint32_t flags) {
    if (flags & (1U << 9)) sti();
    else cli();
}

static void smp_wait_ticks(uint32_t ticks) {
    uint32_t flags = smp_save_flags();
    uint32_t start = pit_get_ticks();
    sti();
    while ((uint32_t)(pit_get_ticks() - start) < ticks)
        __asm__ volatile ("hlt" : : : "memory");
    smp_restore_flags(flags);
}

static uint32_t smp_ticks_from_ms(uint32_t milliseconds) {
    uint32_t hz = pit_get_frequency_hz();
    uint32_t ticks;
    if (!hz) hz = 100U;
    ticks = (hz * milliseconds + 999U) / 1000U;
    return ticks ? ticks : 1U;
}

static void smp_wait_milliseconds(uint32_t milliseconds) {
    smp_wait_ticks(smp_ticks_from_ms(milliseconds));
}

static void lapic_enable_local(void) {
    uint64_t base = rdmsr(IA32_APIC_BASE_MSR);
    uint32_t requested = g_lapic_phys
        ? (g_lapic_phys & 0xFFFFF000U)
        : (uint32_t)(base & 0xFFFFF000ULL);
    base &= ~0xFFFFF000ULL;
    base |= (uint64_t)requested | IA32_APIC_BASE_ENABLE;
    wrmsr(IA32_APIC_BASE_MSR, base);
    g_lapic_phys = requested;
    g_lapic = (volatile uint32_t *)(uintptr_t)requested;
    lapic_write(LAPIC_TPR, 0U);
    lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | SMP_LAPIC_SPURIOUS_VECTOR);
    lapic_write(LAPIC_ESR, 0U);
    lapic_write(LAPIC_ESR, 0U);
}

static uint8_t lapic_id(void) {
    return (uint8_t)(lapic_read(LAPIC_ID) >> 24U);
}

static void add_cpu(uint8_t apic_id, bool bsp_hint) {
    for (uint32_t i = 0U; i < g_cpu_count; i++)
        if (g_cpus[i].present && g_cpus[i].apic_id == apic_id) {
            if (bsp_hint) g_cpus[i].bsp = 1U;
            return;
        }
    if (g_cpu_count >= SMP_MAX_CPUS) return;
    g_cpus[g_cpu_count].apic_id = apic_id;
    g_cpus[g_cpu_count].present = 1U;
    g_cpus[g_cpu_count].online = 0U;
    g_cpus[g_cpu_count].bsp = bsp_hint ? 1U : 0U;
    g_cpu_count++;
}

static void reset_cpu_list(uint8_t bsp_apic_id) {
    kmemset(g_cpus, 0, sizeof(g_cpus));
    kmemset(g_apic_to_cpu, SMP_INVALID_CPU, sizeof(g_apic_to_cpu));
    /* No confíe en el valor residual de una sesión de init ni en el contador
       incremental de los AP. El BSP ya está online al reconstruir la lista. */
    g_online_count = 1U;
    g_smp_available = false;
    g_release_aps = 0U;
    g_scheduler_started = 0U;
    g_cpu_count = 1U;
    g_cpus[0].apic_id = bsp_apic_id;
    g_cpus[0].present = 1U;
    g_cpus[0].online = 1U;
    g_cpus[0].bsp = 1U;
    g_apic_to_cpu[bsp_apic_id] = 0U;
}

static void normalize_bsp_first(uint8_t bsp_apic_id) {
    uint32_t bsp_index = 0U;
    for (uint32_t i = 0U; i < g_cpu_count; i++) {
        if (g_cpus[i].apic_id == bsp_apic_id) {
            bsp_index = i;
            break;
        }
    }
    if (bsp_index != 0U) {
        smp_cpu_desc_t tmp = g_cpus[0];
        g_cpus[0] = g_cpus[bsp_index];
        g_cpus[bsp_index] = tmp;
    }
    g_cpus[0].bsp = 1U;
    g_cpus[0].online = 1U;
    for (uint32_t i = 0U; i < 256U; i++) g_apic_to_cpu[i] = SMP_INVALID_CPU;
    for (uint32_t i = 0U; i < g_cpu_count; i++)
        g_apic_to_cpu[g_cpus[i].apic_id] = (uint8_t)i;
}

static const acpi_rsdp_t *find_rsdp_range(uint32_t start, uint32_t end) {
    start = (start + 15U) & ~15U;
    for (uint32_t address = start; address + sizeof(acpi_rsdp_t) <= end;
         address += 16U) {
        const acpi_rsdp_t *rsdp = (const acpi_rsdp_t *)(uintptr_t)address;
        const uint32_t *sig = (const uint32_t *)(const void *)rsdp->signature;
        if (sig[0] != RSDP_SIGNATURE_LOW || sig[1] != RSDP_SIGNATURE_HIGH)
            continue;
        if (checksum8(rsdp, 20U) == 0U) return rsdp;
    }
    return NULL;
}

static const acpi_rsdp_t *find_rsdp(void) {
    uint16_t ebda_segment = mm_boot_ebda_segment();
    uint32_t ebda = (uint32_t)ebda_segment << 4U;
    const acpi_rsdp_t *rsdp = NULL;
    if (ebda >= 0x80000U && ebda < 0xA0000U)
        rsdp = find_rsdp_range(ebda, ebda + 1024U);
    if (!rsdp) rsdp = find_rsdp_range(0xE0000U, 0x100000U);
    return rsdp;
}

static bool parse_acpi_madt(void) {
    const acpi_rsdp_t *rsdp = find_rsdp();
    const acpi_sdt_header_t *rsdt;
    const acpi_madt_t *madt = NULL;
    uint32_t entries;

    if (!rsdp || !rsdp->rsdt_address) return false;
    rsdt = (const acpi_sdt_header_t *)(uintptr_t)rsdp->rsdt_address;
    if (rsdt->signature != ACPI_SIG_RSDT ||
        rsdt->length < sizeof(*rsdt) || checksum8(rsdt, rsdt->length) != 0U)
        return false;
    entries = (rsdt->length - sizeof(*rsdt)) / sizeof(uint32_t);
    const uint32_t *table = (const uint32_t *)((const uint8_t *)rsdt + sizeof(*rsdt));
    for (uint32_t i = 0U; i < entries; i++) {
        const acpi_sdt_header_t *header =
            (const acpi_sdt_header_t *)(uintptr_t)table[i];
        if (!header || header->length < sizeof(*header)) continue;
        if (header->signature == ACPI_SIG_APIC &&
            checksum8(header, header->length) == 0U) {
            madt = (const acpi_madt_t *)(const void *)header;
            break;
        }
    }
    if (!madt || madt->header.length < sizeof(*madt)) return false;
    if (madt->lapic_address) {
        g_lapic_phys = madt->lapic_address;
        g_lapic = (volatile uint32_t *)(uintptr_t)g_lapic_phys;
    }

    const uint8_t *entry = (const uint8_t *)madt + sizeof(*madt);
    const uint8_t *end = (const uint8_t *)madt + madt->header.length;
    while (entry + 2U <= end) {
        uint8_t type = entry[0];
        uint8_t length = entry[1];
        if (length < 2U || entry + length > end) break;
        if (type == 0U && length >= 8U) {
            uint8_t apic = entry[3];
            uint32_t flags = *(const uint32_t *)(const void *)(entry + 4U);
            if (flags & 1U) add_cpu(apic, false);
        } else if (type == 5U && length >= 12U) {
            uint64_t override = *(const uint64_t *)(const void *)(entry + 4U);
            if ((override >> 32U) == 0U) {
                g_lapic_phys = (uint32_t)override;
                g_lapic = (volatile uint32_t *)(uintptr_t)g_lapic_phys;
            }
        }
        entry += length;
    }
    return g_cpu_count > 1U;
}

static const mp_floating_t *find_mp_range(uint32_t start, uint32_t length) {
    start = (start + 15U) & ~15U;
    uint32_t end = start + length;
    for (uint32_t address = start; address + sizeof(mp_floating_t) <= end;
         address += 16U) {
        const mp_floating_t *mp = (const mp_floating_t *)(uintptr_t)address;
        if (mp->signature != MP_FLOAT_SIGNATURE || mp->length_units == 0U)
            continue;
        if (checksum8(mp, (uint32_t)mp->length_units * 16U) == 0U) return mp;
    }
    return NULL;
}

static const mp_floating_t *find_mp_floating(void) {
    uint16_t ebda_segment = mm_boot_ebda_segment();
    uint16_t base_kb = mm_boot_conventional_kb();
    const mp_floating_t *mp = NULL;
    if (ebda_segment) mp = find_mp_range((uint32_t)ebda_segment << 4U, 1024U);
    if (!mp && base_kb) {
        uint32_t top = (uint32_t)base_kb * 1024U;
        if (top >= 1024U) mp = find_mp_range(top - 1024U, 1024U);
    }
    if (!mp) mp = find_mp_range(0xF0000U, 0x10000U);
    return mp;
}

static bool parse_mp_tables(void) {
    const mp_floating_t *floating = find_mp_floating();
    if (!floating) return false;
    /* MP Specification default configurations describe a two-processor
     * system without a PCMP table. This matters on early 1990s firmware. */
    if (!floating->config_table) {
        if (floating->feature[0] >= 1U && floating->feature[0] <= 7U) {
            add_cpu(g_cpus[0].apic_id == 0U ? 1U : 0U, false);
            return g_cpu_count > 1U;
        }
        return false;
    }
    const mp_config_t *config =
        (const mp_config_t *)(uintptr_t)floating->config_table;
    if (config->signature != MP_CONFIG_SIGNATURE ||
        config->base_length < sizeof(*config) ||
        checksum8(config, config->base_length) != 0U) return false;
    if (config->lapic_address) {
        g_lapic_phys = config->lapic_address;
        g_lapic = (volatile uint32_t *)(uintptr_t)g_lapic_phys;
    }
    const uint8_t *entry = (const uint8_t *)config + sizeof(*config);
    const uint8_t *end = (const uint8_t *)config + config->base_length;
    for (uint32_t i = 0U; i < config->entry_count && entry < end; i++) {
        uint8_t type = entry[0];
        uint32_t length = type == 0U ? 20U : 8U;
        if (entry + length > end) break;
        if (type == 0U) {
            const mp_processor_t *processor =
                (const mp_processor_t *)(const void *)entry;
            if (processor->flags & 1U)
                add_cpu(processor->apic_id, (processor->flags & 2U) != 0U);
        }
        entry += length;
    }
    return g_cpu_count > 1U;
}

static void build_ap_low_memory(void) {
    uint32_t trampoline_size =
        (uint32_t)((uintptr_t)ap_trampoline_end -
                   (uintptr_t)ap_trampoline_start);
    uint64_t *gdt = (uint64_t *)(uintptr_t)AP_GDT_PHYS;
    struct { uint16_t limit; uint32_t base; } PACKED *descriptor =
        (void *)(uintptr_t)AP_GDT_DESC_PHYS;

    if (trampoline_size > AP_TRAMPOLINE_LIMIT - AP_TRAMPOLINE_PHYS)
        trampoline_size = AP_TRAMPOLINE_LIMIT - AP_TRAMPOLINE_PHYS;
    kmemset((void *)(uintptr_t)AP_TRAMPOLINE_PHYS, 0,
            AP_MAILBOX_PHYS + sizeof(ap_mailbox_t) - AP_TRAMPOLINE_PHYS);
    kmemcpy((void *)(uintptr_t)AP_TRAMPOLINE_PHYS,
            ap_trampoline_start, trampoline_size);

    gdt[0] = 0x0000000000000000ULL;
    gdt[1] = 0x00CF9A000000FFFFULL;
    gdt[2] = 0x00CF92000000FFFFULL;
    descriptor->limit = 3U * 8U - 1U;
    descriptor->base = AP_GDT_PHYS;
}

static uint32_t current_cr3(void) {
    uint32_t cr3;
    __asm__ volatile ("movl %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static void lapic_send(uint8_t destination, uint32_t low) {
    lapic_wait_delivery();
    lapic_write(LAPIC_ICR_HIGH, (uint32_t)destination << 24U);
    lapic_write(LAPIC_ICR_LOW, low);
    lapic_wait_delivery();
}

void smp_reschedule_cpu(uint32_t cpu_index) {
    uint32_t self;
    if (!g_scheduler_started || cpu_index >= g_cpu_count ||
        !g_cpus[cpu_index].online) return;
    self = smp_cpu_index();
    if (cpu_index == self) return;
    lapic_send(g_cpus[cpu_index].apic_id, SMP_RESCHEDULE_VECTOR);
}

void smp_reschedule_mask(uint32_t cpu_mask) {
    uint32_t limit = g_cpu_count > 32U ? 32U : g_cpu_count;
    for (uint32_t cpu = 0U; cpu < limit; cpu++)
        if (cpu_mask & (1U << cpu)) smp_reschedule_cpu(cpu);
}

static bool boot_ap(uint32_t cpu_index) {
    ap_mailbox_t *mailbox = (ap_mailbox_t *)(uintptr_t)AP_MAILBOX_PHYS;
    smp_cpu_desc_t *cpu = &g_cpus[cpu_index];
    uint32_t deadline;

    cpu->stack = (uint32_t *)mm_alloc_guarded_stack(
        AP_STACK_SIZE, false, &cpu->stack_allocation, &cpu->stack_size);
    if (!cpu->stack) return false;
    if (!task_smp_prepare_cpu(cpu_index, cpu->stack_allocation,
                              cpu->stack, cpu->stack_size)) {
        mm_free_guarded_stack(cpu->stack_allocation, cpu->stack,
                              cpu->stack_size);
        cpu->stack = NULL;
        cpu->stack_allocation = NULL;
        cpu->stack_size = 0U;
        return false;
    }

    mailbox->cr3 = current_cr3();
    mailbox->stack_top = (uint32_t)(uintptr_t)
        ((uint8_t *)cpu->stack + cpu->stack_size - 16U);
    mailbox->entry = (uint32_t)(uintptr_t)smp_ap_entry;
    mailbox->cpu_index = cpu_index;
    mailbox->acknowledged = 0U;

    lapic_write(LAPIC_ESR, 0U);
    lapic_send(cpu->apic_id, LAPIC_DM_INIT | LAPIC_ICR_LEVEL_ASSERT |
               LAPIC_ICR_TRIGGER_LEVEL);
    smp_wait_milliseconds(10U);
    lapic_send(cpu->apic_id, LAPIC_DM_INIT | LAPIC_ICR_TRIGGER_LEVEL);
    smp_short_delay(300U);
    lapic_send(cpu->apic_id, LAPIC_DM_STARTUP | AP_STARTUP_VECTOR);
    smp_short_delay(3000U);
    lapic_send(cpu->apic_id, LAPIC_DM_STARTUP | AP_STARTUP_VECTOR);

    deadline = pit_get_ticks() + smp_ticks_from_ms(SMP_BOOT_TIMEOUT_MS);
    {
        uint32_t flags = smp_save_flags();
        sti();
        while (!cpu->online && (int32_t)(pit_get_ticks() - deadline) < 0)
            __asm__ volatile ("hlt" : : : "memory");
        smp_restore_flags(flags);
    }
    return cpu->online != 0U;
}

static void calibrate_lapic_timer(void) {
    uint32_t flags = smp_save_flags();
    uint32_t start_tick;
    uint32_t elapsed;
    uint32_t current;

    sti();
    lapic_write(LAPIC_TIMER_DIVIDE, 0x3U); /* divide by 16 */
    lapic_write(LAPIC_LVT_TIMER,
                LAPIC_LVT_MASKED | SMP_LAPIC_TIMER_VECTOR);
    start_tick = pit_get_ticks();
    while (pit_get_ticks() == start_tick)
        __asm__ volatile ("hlt" : : : "memory");
    start_tick = pit_get_ticks();
    lapic_write(LAPIC_TIMER_INITIAL, 0xFFFFFFFFU);
    while ((uint32_t)(pit_get_ticks() - start_tick) < SMP_TIMER_CAL_TICKS)
        __asm__ volatile ("hlt" : : : "memory");
    current = lapic_read(LAPIC_TIMER_CURRENT);
    elapsed = 0xFFFFFFFFU - current;
    if (elapsed > SMP_TIMER_CAL_TICKS)
        g_lapic_timer_initial = elapsed / SMP_TIMER_CAL_TICKS;
    if (g_lapic_timer_initial < 1000U) g_lapic_timer_initial = 1000U;
    lapic_write(LAPIC_LVT_TIMER,
                LAPIC_LVT_MASKED | SMP_LAPIC_TIMER_VECTOR);
    smp_restore_flags(flags);
}

static void lapic_timer_start(void) {
    lapic_write(LAPIC_TIMER_DIVIDE, 0x3U);
    lapic_write(LAPIC_LVT_TIMER,
                LAPIC_TIMER_PERIODIC | SMP_LAPIC_TIMER_VECTOR);
    lapic_write(LAPIC_TIMER_INITIAL, g_lapic_timer_initial);
}

void smp_lapic_eoi(void) {
    lapic_write(LAPIC_EOI, 0U);
}

registers_t *smp_lapic_timer_interrupt(registers_t *frame) {
    return task_schedule_lapic(frame);
}

registers_t *smp_reschedule_interrupt(registers_t *frame) {
    return task_schedule_reschedule_ipi(frame);
}

registers_t *smp_lapic_spurious_interrupt(registers_t *frame) {
    /* The APIC spurious vector does not require an EOI. */
    return frame;
}

uint32_t smp_cpu_index(void) {
    uint32_t cpu;
    /* STR es una instruccion local y barata. La version anterior leia el ID
     * del LAPIC por MMIO en cada spinlock, kmalloc, acceso a paging y tick del
     * scheduler; en QEMU/WHPX/KVM eso podia costar cientos de ciclos y hacia
     * que todo el sistema pareciera ocupado aunque los AP estuvieran idle. */
    if (!g_scheduler_started) return 0U;
    cpu = gdt_current_cpu_index();
    if (cpu < g_cpu_count) return cpu;

    /* Fallback defensivo para un CPU que entre antes de cargar su GDT/TSS. */
    if (g_lapic) {
        uint8_t index = g_apic_to_cpu[lapic_id()];
        if (index != SMP_INVALID_CPU) return index;
    }
    return 0U;
}

uint32_t smp_cpu_count(void) { return g_cpu_count; }
uint32_t smp_online_cpu_count(void) { return g_online_count; }
bool smp_cpu_online(uint32_t cpu_index) {
    return cpu_index < g_cpu_count && g_cpus[cpu_index].online != 0U;
}
uint8_t smp_cpu_apic_id(uint32_t cpu_index) {
    return cpu_index < g_cpu_count ? g_cpus[cpu_index].apic_id : 0U;
}
bool smp_is_available(void) { return g_smp_available; }
bool smp_scheduler_started(void) { return g_scheduler_started != 0U; }
bool smp_is_bsp(void) { return smp_cpu_index() == 0U; }

static uint32_t atomic_xchg(volatile uint32_t *address, uint32_t value) {
    __asm__ volatile ("xchgl %0, %1"
                      : "+r"(value), "+m"(*address) : : "memory", "cc");
    return value;
}

void smp_kernel_enter(void) {
    uint32_t cpu;
    if (!g_scheduler_started || g_online_count < 2U) return;
    cpu = smp_cpu_index();
    if (cpu >= SMP_MAX_CPUS) cpu = 0U;
    if (g_kernel_owner == (uint8_t)cpu) {
        if (g_kernel_depth[cpu] != 0xFFU) g_kernel_depth[cpu]++;
        return;
    }

    /* CPU0 owns the GUI, PIC IRQs and most legacy drivers. Give it priority
     * over timer-only AP work so many idle cores cannot starve the desktop. */
    if (cpu == 0U)
        __asm__ volatile ("lock incl %0" : "+m"(g_bsp_waiting) : :
                          "memory", "cc");
    while (atomic_xchg(&g_kernel_lock, 1U) != 0U)
        __asm__ volatile ("pause");
    if (cpu == 0U)
        __asm__ volatile ("lock decl %0" : "+m"(g_bsp_waiting) : :
                          "memory", "cc");
    g_kernel_owner = (uint8_t)cpu;
    g_kernel_depth[cpu] = 1U;
    __asm__ volatile ("" : : : "memory");
}

bool smp_kernel_try_enter_timer(void) {
    uint32_t cpu;
    uint32_t expected;

    if (!g_scheduler_started || g_online_count < 2U) return true;
    cpu = smp_cpu_index();
    if (cpu >= SMP_MAX_CPUS) cpu = 0U;

    if (g_kernel_owner == (uint8_t)cpu) {
        if (g_kernel_depth[cpu] != 0xFFU) g_kernel_depth[cpu]++;
        return true;
    }

    /* A Local APIC timer is advisory: missing one quantum is harmless.
     * Waiting here is not harmless. With 16 APs the old code left every idle
     * CPU spinning on the same cache line while CPU0 tried to finish boot. */
    if (cpu != 0U && g_bsp_waiting) return false;

    expected = 0U;
    __asm__ volatile ("lock cmpxchgl %2, %1"
                      : "+a"(expected), "+m"(g_kernel_lock)
                      : "r"(1U) : "memory", "cc");
    if (expected != 0U) return false;

    g_kernel_owner = (uint8_t)cpu;
    g_kernel_depth[cpu] = 1U;
    __asm__ volatile ("" : : : "memory");
    return true;
}

void smp_kernel_enter_interrupt(void) {
    uint32_t flags;

    if (!g_scheduler_started || g_online_count < 2U) return;

    /*
     * int 0x80 is a trap gate, so IF normally remains set.  Spinning for the
     * global kernel lock with IF=1 lets the LAPIC timer interrupt the spin,
     * whose nested handler then spins for the same lock again.  Repetition
     * consumes the kernel stack and can leave both CPUs apparently frozen
     * immediately after the first Ring-3 service starts.
     *
     * Mask interrupts only while ownership is contested.  Once this CPU owns
     * the recursive BKL, restore the entry IF state: device IRQs and the timer
     * may then nest safely because smp_kernel_enter() recognizes the owner.
     * Interrupt-gate entries arrive with IF=0 and therefore remain masked.
     */
    flags = smp_save_flags();
    cli();
    smp_kernel_enter();
    smp_restore_flags(flags);
}

static void smp_kernel_release_one(uint32_t cpu) {
    if (cpu >= SMP_MAX_CPUS || g_kernel_owner != (uint8_t)cpu ||
        g_kernel_depth[cpu] == 0U) return;
    if (--g_kernel_depth[cpu] != 0U) return;
    __asm__ volatile ("" : : : "memory");
    g_kernel_owner = SMP_INVALID_CPU;
    g_kernel_lock = 0U;
}

bool smp_kernel_locked_by_current_cpu(void) {
    uint32_t cpu = smp_cpu_index();
    return cpu < SMP_MAX_CPUS && g_kernel_owner == (uint8_t)cpu &&
           g_kernel_depth[cpu] != 0U;
}

void smp_kernel_exit_frame(registers_t *frame) {
    uint32_t cpu;
    uint8_t desired;
    if (!g_scheduler_started || g_online_count < 2U) return;
    cpu = smp_cpu_index();
    if (cpu >= SMP_MAX_CPUS || g_kernel_owner != (uint8_t)cpu) return;
    (void)frame;
    /* In the domain-lock kernel, an external IRQ must not leave one recursive
     * level of the legacy BKL attached to the interrupted CPL0 context.  That
     * rule belonged to the old all-kernel lock model and can permanently pin
     * the quarantine lock after the first desktop interrupt. */
    desired = 0U;
    while (g_kernel_depth[cpu] > desired) smp_kernel_release_one(cpu);
}

void smp_kernel_relax(void) {
    uint32_t cpu;
    if (!g_scheduler_started || g_online_count < 2U) return;
    cpu = smp_cpu_index();
    if (cpu >= SMP_MAX_CPUS || g_kernel_owner != (uint8_t)cpu) return;
    while (g_kernel_depth[cpu]) smp_kernel_release_one(cpu);
}

void smp_kernel_reacquire(void) {
    if (!smp_kernel_locked_by_current_cpu()) smp_kernel_enter();
}

void smp_ap_entry(uint32_t cpu_index) {
    if (cpu_index == 0U || cpu_index >= g_cpu_count) {
        for (;;) __asm__ volatile ("cli; hlt");
    }
    lapic_enable_local();
    g_apic_to_cpu[lapic_id()] = (uint8_t)cpu_index;
    gdt_init_ap(cpu_index);
    idt_load_current();
    task_fpu_prepare_secondary_cpu();
    g_cpus[cpu_index].online = 1U;
    __asm__ volatile ("lock incl %0" : "+m"(g_online_count) : : "memory", "cc");
    ((ap_mailbox_t *)(uintptr_t)AP_MAILBOX_PHYS)->acknowledged = 1U;

    while (!g_release_aps) __asm__ volatile ("pause");
    if (!g_scheduler_started) {
        sti();
        for (;;) __asm__ volatile ("hlt" : : : "memory");
    }
    /* task_smp_ap_online() protects the shared scheduler table with its own
     * short IRQ-safe lock. AP bootstrap no longer passes through the legacy
     * kernel-wide quarantine lock. */
    task_smp_ap_online(cpu_index);

    /* Do not inject the first scheduler interrupt on every AP at the same
     * instant.  Sixteen simultaneous software INT 0xF0 entries were a
     * reliable way to create a scheduler-lock herd on WHPX.  A short per-CPU
     * phase delay spreads the first periodic LAPIC ticks; afterwards AP timer
     * admission is non-blocking, so a busy scheduler simply costs one tick. */
    for (volatile uint32_t phase = 0U;
         phase < cpu_index * 4096U; phase++)
        __asm__ volatile ("pause");
    lapic_timer_start();
    sti();
    for (;;) __asm__ volatile ("hlt" : : : "memory");
}

void smp_init(void) {
    uint32_t eax, ebx, ecx, edx;
    uint64_t apic_base;
    uint8_t bsp_apic;
    bool found;

    cpuid(1U, &eax, &ebx, &ecx, &edx);
    (void)eax; (void)ebx; (void)ecx;
    if ((edx & CPUID_EDX_APIC) == 0U || !paging_is_enabled()) {
        kprintf("[SMP] Local APIC no disponible; modo monoprocesador\n");
        return;
    }

    apic_base = rdmsr(IA32_APIC_BASE_MSR);
    g_lapic_phys = (uint32_t)(apic_base & 0xFFFFF000ULL);
    g_lapic = (volatile uint32_t *)(uintptr_t)g_lapic_phys;
    lapic_enable_local();
    bsp_apic = lapic_id();
    reset_cpu_list(bsp_apic);

    found = parse_acpi_madt();
    if (!found) found = parse_mp_tables();
    lapic_enable_local();
    normalize_bsp_first(bsp_apic);
    if (!found || g_cpu_count < 2U) {
        kprintf("[SMP] CPU BSP APIC=%u; no se detectaron procesadores secundarios\n",
                bsp_apic);
        g_cpu_count = 1U;
        return;
    }

    build_ap_low_memory();
    calibrate_lapic_timer();
    kprintf("[SMP] topologia detectada: %u CPU(s), BSP APIC=%u, LAPIC=%x\n",
            g_cpu_count, bsp_apic, g_lapic_phys);

    for (uint32_t cpu = 1U; cpu < g_cpu_count; cpu++) {
        kprintf("[SMP] iniciando CPU%u APIC=%u...\n", cpu, g_cpus[cpu].apic_id);
        if (boot_ap(cpu))
            kprintf("[SMP] CPU%u online\n", cpu);
        else
            kprintf("[SMP] CPU%u no respondio; se excluye\n", cpu);
    }
    /* La bandera per-CPU es la fuente de verdad. Esto también repara kernels
       donde el contador arrancó accidentalmente en cero: el AP estaba vivo,
       pero el log mostraba 1/2 y el scheduler SMP quedaba deshabilitado. */
    g_online_count = 0U;
    for (uint32_t cpu = 0U; cpu < g_cpu_count; cpu++)
        if (g_cpus[cpu].online) g_online_count++;
    g_smp_available = g_online_count > 1U;
    kprintf("[SMP] %u/%u CPU(s) online; timer LAPIC=%u cuentas/tick\n",
            g_online_count, g_cpu_count, g_lapic_timer_initial);
}

void smp_start_scheduler(void) {
    uint32_t flags;
    if (!g_smp_available || g_online_count < 2U) return;
    /* Publish the per-CPU scheduler atomically. The legacy lock starts free:
     * only unclassified/Win16 paths acquire it after the fine-grained split. */
    flags = smp_save_flags();
    cli();
    g_kernel_lock = 0U;
    g_kernel_owner = SMP_INVALID_CPU;
    kmemset(g_kernel_depth, 0, sizeof(g_kernel_depth));
    g_bsp_waiting = 0U;
    if (!task_smp_scheduler_start()) {
        kprintf("[SMP] no se pudo iniciar scheduler por CPU; se conserva CPU0\n");
        g_release_aps = 1U;
        smp_restore_flags(flags);
        return;
    }
    g_scheduler_started = 1U;
    __asm__ volatile ("" : : : "memory");
    g_release_aps = 1U;
    smp_restore_flags(flags);
    kprintf("[SMP] scheduler por runqueue activo en %u CPU(s); work stealing + IPI\n",
            g_online_count);
}
