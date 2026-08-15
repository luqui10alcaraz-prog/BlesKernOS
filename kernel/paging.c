#include "include/paging.h"
#include "include/memory.h"
#include "include/smp.h"
#include "include/klock.h"
#include "include/vga.h"

#define PAGE_PRESENT       0x001U
#define PAGE_WRITABLE      0x002U
#define PAGE_USER          0x004U
#define PAGE_WRITE_THROUGH 0x008U
#define PAGE_CACHE_DISABLE 0x010U
#define PAGE_4M            0x080U
#define PAGE_FRAME_4K      0xFFFFF000U
#define PAGE_FRAME_4M      0xFFC00000U
#define CR0_WP             0x00010000U
#define CR0_PG             0x80000000U
#define CR4_PSE            0x00000010U
#define CPUID_PSE          0x00000008U

/* Most of the identity map stays in 4 MiB pages. Only the first 4 MiB and
 * PDEs containing guard pages are split. A small supervisor-only pool avoids
 * putting page tables inside the user-visible heap. Eight tables cover 32 MiB
 * of scattered stack allocations, which is enough for TASK_MAX=32 because
 * the allocator packs stacks contiguously. */
#define PAGING_SPLIT_TABLES 8U

static uint32_t kernel_page_directory[1024] __attribute__((aligned(4096)));
static uint32_t low_page_table[1024] __attribute__((aligned(4096)));
static uint32_t split_page_tables[PAGING_SPLIT_TABLES][1024]
    __attribute__((aligned(4096)));
static uint16_t split_owner[PAGING_SPLIT_TABLES];
static bool paging_enabled;
static volatile uint32_t g_tlb_generation = 1U;
static volatile uint32_t g_tlb_seen[SMP_MAX_CPUS];
static kspinlock_t g_paging_lock = KSPINLOCK_INITIALIZER;

static bool paging_cpuid_available(void) {
    uint32_t before, toggled, after;
    __asm__ volatile ("pushfl; popl %0" : "=r"(before));
    toggled = before ^ (1U << 21);
    __asm__ volatile ("pushl %0; popfl" : : "r"(toggled) : "cc");
    __asm__ volatile ("pushfl; popl %0" : "=r"(after));
    __asm__ volatile ("pushl %0; popfl" : : "r"(before) : "cc");
    return ((after ^ before) & (1U << 21)) != 0U;
}

static uint32_t paging_cpu(void) {
    uint32_t cpu = smp_cpu_index();
    return cpu < SMP_MAX_CPUS ? cpu : 0U;
}

static void paging_reload_cr3(void) {
    uint32_t cr3;
    __asm__ volatile ("movl %%cr3,%0" : "=r"(cr3));
    __asm__ volatile ("movl %0,%%cr3" : : "r"(cr3) : "memory");
}

static void paging_invlpg(uint32_t address) {
    __asm__ volatile ("invlpg (%0)" : : "r"(address) : "memory");
}

static uint32_t paging_publish_change(void) {
    uint32_t generation;
    __asm__ volatile ("lock incl %0" : "+m"(g_tlb_generation) : :
                      "memory", "cc");
    generation = g_tlb_generation;
    paging_reload_cr3();
    g_tlb_seen[paging_cpu()] = generation;
    return generation;
}

static void paging_kick_generation(void) {
    uint32_t mask = 0U;
    uint32_t self;
    uint32_t online;

    if (!smp_scheduler_started()) return;
    self = paging_cpu();
    online = smp_cpu_count();
    if (online > 32U) online = 32U;
    for (uint32_t cpu = 0U; cpu < online; cpu++) {
        if (cpu != self && smp_cpu_online(cpu)) mask |= 1U << cpu;
    }
    /* El IPI de replanificacion entra por task_schedule_internal(), cuyo
     * primer paso es paging_sync_cpu(). Antes solo se esperaba al siguiente
     * tick de cada AP, bloqueando al creador de una tarea hasta millones de
     * iteraciones por cada pagina guard. */
    if (mask) smp_reschedule_mask(mask);
}

static void paging_wait_generation(uint32_t generation) {
    uint32_t online;
    uint32_t spins = 2000000U;

    if (!smp_scheduler_started()) return;
    online = smp_cpu_count();
    if (online > SMP_MAX_CPUS) online = SMP_MAX_CPUS;
    while (spins--) {
        bool done = true;
        for (uint32_t cpu = 0U; cpu < online; cpu++) {
            if (!smp_cpu_online(cpu)) continue;
            if (g_tlb_seen[cpu] < generation) {
                done = false;
                break;
            }
        }
        if (done) return;
        __asm__ volatile ("pause");
    }
    kprintf("[PAGING] aviso: shootdown TLB incompleto gen=%u online=%u\n",
            generation, online);
}

static uint32_t *paging_split_pde_locked(uint32_t pde_index) {
    uint32_t pde;
    uint32_t base;
    uint32_t flags;
    uint32_t *table = NULL;

    if (pde_index >= 1024U) return NULL;
    pde = kernel_page_directory[pde_index];
    if ((pde & PAGE_PRESENT) && !(pde & PAGE_4M))
        return (uint32_t *)(uintptr_t)(pde & PAGE_FRAME_4K);

    for (uint32_t i = 0U; i < PAGING_SPLIT_TABLES; i++) {
        if (split_owner[i] == (uint16_t)(pde_index + 1U))
            return split_page_tables[i];
        if (!split_owner[i] && !table) {
            table = split_page_tables[i];
            split_owner[i] = (uint16_t)(pde_index + 1U);
        }
    }
    if (!table) return NULL;

    base = pde & PAGE_FRAME_4M;
    flags = pde & (PAGE_WRITABLE | PAGE_USER |
                   PAGE_WRITE_THROUGH | PAGE_CACHE_DISABLE);
    for (uint32_t i = 0U; i < 1024U; i++)
        table[i] = (base + i * PAGING_PAGE_SIZE) |
                   PAGE_PRESENT | flags;
    kernel_page_directory[pde_index] =
        ((uint32_t)(uintptr_t)table & PAGE_FRAME_4K) |
        PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    return table;
}

static uint32_t paging_entry_flags(uint32_t address, bool *large_out) {
    uint32_t pde = kernel_page_directory[address >> 22U];
    if (!(pde & PAGE_PRESENT)) {
        if (large_out) *large_out = false;
        return 0U;
    }
    if (pde & PAGE_4M) {
        if (large_out) *large_out = true;
        return pde;
    }
    if (large_out) *large_out = false;
    return ((uint32_t *)(uintptr_t)(pde & PAGE_FRAME_4K))
        [(address >> 12U) & 1023U];
}

bool paging_init(void) {
    uint32_t eax = 1U, ebx, ecx, edx;
    uint32_t cr0, cr4;

    if (!paging_cpuid_available()) {
        kprintf("[PAGING] CPU sin CPUID; paging no activado\n");
        return false;
    }

    __asm__ volatile ("cpuid"
        : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    (void)ebx; (void)ecx;
    if ((edx & CPUID_PSE) == 0U) {
        kprintf("[PAGING] CPU sin paginas de 4 MiB; paging no activado\n");
        return false;
    }

    for (uint32_t i = 0U; i < 1024U; i++)
        kernel_page_directory[i] = (i << 22U) | PAGE_PRESENT |
            PAGE_WRITABLE | PAGE_4M;

    /* Page zero is deliberately absent. Everything else below 4 MiB is
     * supervisor-only: kernel image, GDT/IDT, AP trampoline and static SMP
     * state can no longer be overwritten directly from Ring 3. */
    low_page_table[0] = 0U;
    for (uint32_t i = 1U; i < 1024U; i++)
        low_page_table[i] = i * PAGING_PAGE_SIZE |
            PAGE_PRESENT | PAGE_WRITABLE;
    kernel_page_directory[0] =
        ((uint32_t)(uintptr_t)low_page_table & PAGE_FRAME_4K) |
        PAGE_PRESENT | PAGE_WRITABLE;

    kmemset(split_owner, 0, sizeof(split_owner));
    kmemset((void *)g_tlb_seen, 0, sizeof(g_tlb_seen));
    g_tlb_seen[0] = g_tlb_generation;

    /* 0xFEC00000-0xFEFFFFFF contains IOAPIC/LAPIC MMIO. It remains
     * supervisor-only and uncached. */
    kernel_page_directory[0xFEC00000U >> 22U] |=
        PAGE_WRITE_THROUGH | PAGE_CACHE_DISABLE;

    __asm__ volatile ("movl %%cr4,%0" : "=r"(cr4));
    cr4 |= CR4_PSE;
    __asm__ volatile ("movl %0,%%cr4" : : "r"(cr4) : "memory");
    __asm__ volatile ("movl %0,%%cr3" : :
        "r"((uint32_t)(uintptr_t)kernel_page_directory) : "memory");
    __asm__ volatile ("movl %%cr0,%0" : "=r"(cr0));
    cr0 |= CR0_PG | CR0_WP;
    __asm__ volatile ("movl %0,%%cr0" : : "r"(cr0) : "memory");
    __asm__ volatile ("jmp 1f\n1:" : : : "memory");
    paging_enabled = true;
    kprintf("[PAGING] activo: kernel supervisor, pagina cero guard, CR0.WP\n");
    return true;
}

bool paging_configure_user_layout(uint32_t user_start, uint32_t user_end) {
    if (!paging_enabled || user_end <= user_start) return false;
    /* PE fixed view and managed heap are the only flat Ring-3 ranges in this
     * transitional address space. The operation also handles a partial final
     * PDE using the supervisor-only split-table pool. */
    if (!paging_set_range(PE_FIXED_VIEW_START,
                          PE_FIXED_VIEW_END - PE_FIXED_VIEW_START,
                          true, true, true)) return false;
    if (!paging_set_range(user_start, user_end - user_start,
                          true, true, true)) return false;
    if (paging_page_present(0U) || paging_page_user(0x00100000U) ||
        !paging_page_user(PE_FIXED_VIEW_START) ||
        !paging_page_user(user_start)) {
        kprintf("[PAGING] ERROR: autoprueba supervisor/user fallo\n");
        return false;
    }
    kprintf("[PAGING] Ring 3 permitido: %x-%x y heap %x-%x\n",
            PE_FIXED_VIEW_START, PE_FIXED_VIEW_END,
            user_start, user_end);
    return true;
}

bool paging_set_range(uint32_t address, uint32_t length, bool present,
                      bool user, bool writable) {
    uint32_t first;
    uint32_t end;
    uint32_t flags;
    uint32_t lock_flags;
    uint32_t generation;

    if (!paging_enabled || !length) return false;
    if (address + length < address) return false;
    first = address & ~(PAGING_PAGE_SIZE - 1U);
    end = (address + length + PAGING_PAGE_SIZE - 1U) &
          ~(PAGING_PAGE_SIZE - 1U);
    flags = (present ? PAGE_PRESENT : 0U) |
            (writable ? PAGE_WRITABLE : 0U) |
            (user ? PAGE_USER : 0U);

    lock_flags = kspin_lock_irqsave(&g_paging_lock);
    for (uint32_t page = first; page < end;) {
        uint32_t pde_index = page >> 22U;
        uint32_t old_pde = kernel_page_directory[pde_index];
        uint32_t remaining = end - page;

        /* Preserve the compact 4 MiB identity mapping whenever the request
         * covers a complete, still-large PDE. This is essential for marking
         * a 120 MiB heap user-accessible without consuming one 4 KiB page
         * table for every 4 MiB. Guard pages and partial final ranges alone
         * force a split. A PDE that is already split is never collapsed,
         * because another stack guard may live in the same 4 MiB region. */
        if ((old_pde & PAGE_4M) &&
            (page & 0x003FFFFFU) == 0U &&
            remaining >= 0x00400000U) {
            uint32_t physical = old_pde & PAGE_FRAME_4M;
            uint32_t cache_flags = old_pde &
                (PAGE_WRITE_THROUGH | PAGE_CACHE_DISABLE);
            kernel_page_directory[pde_index] = physical | cache_flags |
                PAGE_4M | flags;
            page += 0x00400000U;
            continue;
        }

        {
            uint32_t *table = paging_split_pde_locked(pde_index);
            uint32_t pte_index;
            uint32_t physical;
            uint32_t cache_flags;
            if (!table) {
                kspin_unlock_irqrestore(&g_paging_lock, lock_flags);
                kprintf("[PAGING] sin tablas para dividir PDE %u addr=%x\n",
                        pde_index, page);
                return false;
            }
            pte_index = (page >> 12U) & 1023U;
            physical = table[pte_index] & PAGE_FRAME_4K;
            cache_flags = table[pte_index] &
                (PAGE_WRITE_THROUGH | PAGE_CACHE_DISABLE);
            table[pte_index] = physical | cache_flags | flags;
            if (user) kernel_page_directory[pde_index] |= PAGE_USER;
            paging_invlpg(page);
            page += PAGING_PAGE_SIZE;
        }
    }
    generation = paging_publish_change();
    kspin_unlock_irqrestore(&g_paging_lock, lock_flags);
    paging_kick_generation();
    paging_wait_generation(generation);
    return true;
}

bool paging_page_present(uint32_t address) {
    return (paging_entry_flags(address, NULL) & PAGE_PRESENT) != 0U;
}

bool paging_page_user(uint32_t address) {
    bool large;
    uint32_t flags = paging_entry_flags(address, &large);
    (void)large;
    return (flags & (PAGE_PRESENT | PAGE_USER)) ==
           (PAGE_PRESENT | PAGE_USER);
}

bool paging_user_range_ok(const void *pointer, uint32_t length,
                          bool require_write) {
    uint32_t start = (uint32_t)(uintptr_t)pointer;
    uint32_t end;
    uint32_t page;

    if (!pointer) return length == 0U;
    if (!length) return paging_page_user(start);
    end = start + length - 1U;
    if (end < start) return false;
    page = start & ~(PAGING_PAGE_SIZE - 1U);
    for (;;) {
        uint32_t flags = paging_entry_flags(page, NULL);
        if ((flags & (PAGE_PRESENT | PAGE_USER)) !=
            (PAGE_PRESENT | PAGE_USER)) return false;
        if (require_write && !(flags & PAGE_WRITABLE)) return false;
        if (page >= (end & ~(PAGING_PAGE_SIZE - 1U))) break;
        page += PAGING_PAGE_SIZE;
    }
    return true;
}

void paging_sync_cpu(void) {
    uint32_t cpu;
    uint32_t generation;
    if (!paging_enabled) return;
    cpu = paging_cpu();
    generation = g_tlb_generation;
    if (g_tlb_seen[cpu] == generation) return;
    paging_reload_cr3();
    g_tlb_seen[cpu] = generation;
}

uint32_t paging_tlb_generation(void) { return g_tlb_generation; }

bool paging_recover_stale_fault(uint32_t address) {
    uint32_t cpu = paging_cpu();
    if (!paging_enabled || !paging_page_present(address) ||
        g_tlb_seen[cpu] == g_tlb_generation) return false;
    paging_sync_cpu();
    kprintf("[PAGING] TLB obsoleto reparado cpu=%u addr=%x gen=%u\n",
            cpu, address, g_tlb_generation);
    return true;
}

bool paging_is_enabled(void) { return paging_enabled; }

uint32_t paging_fault_address(void) {
    uint32_t address;
    __asm__ volatile ("movl %%cr2,%0" : "=r"(address));
    return address;
}
