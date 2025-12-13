#include <lilac/lilac.h>
#include <lilac/boot.h>
#include <lilac/percpu.h>
#include <lilac/timer.h>
#include <mm/kmm.h>
#include <mm/page.h>

#include <asm/segments.h>
#include <asm/msr.h>
#include <asm/cpu-flags.h>

#include "idt.h"
#include "gdt.h"
#include "apic.h"
#include "timer.h"

static inline void set_gs_base(u64 base)
{
    asm volatile (
        "wrmsr" :: "c"(IA32_GS_BASE), "a"((u32)base), "d"((u32)(base >> 32))
    );
}

static inline void set_kernel_gs_base(u64 base)
{
    asm volatile (
        "wrmsr" :: "c"(IA32_KERNEL_GS_BASE), "a"((u32)base), "d"((u32)(base >> 32))
    );
}

static inline void swapgs(void)
{
    asm volatile ("swapgs");
}

uintptr_t __per_cpu_offset[CONFIG_MAX_CPUS];

__section(PERCPU_SECTION".first") __used
struct cpu_local cpu_local_storage = {
    .self = &cpu_local_storage,
    .priv = nullptr,
    .id = 0,
    .scratch = 0,
    .user_stack = nullptr
};

__align(16)
unsigned char ap_stack[0x8000];

#ifndef __x86_64__
void syscall_init(void) {}
#endif

__noreturn
void ap_startup(void)
{
    u8 id = get_lapic_id();
    arch_disable_interrupts();
    tss_init();
    set_tss_esp0((uintptr_t)ap_stack + 0x1000 * (id+1));
    load_idt();
    ap_lapic_enable();
    syscall_init();
    percpu_init_cpu();
    // timer_tick_init();
    klog(LOG_DEBUG, "AP %d started\n", id);
    // arch_enable_interrupts();

    while (1)
        asm ("hlt");
}

extern char _percpu_start;
extern char _percpu_end;

#ifndef __x86_64__
/* call on each CPU to point GS at 'base' */
void set_gs_base_32(uint32_t base)
{
    extern struct gdt_entry gdt_entries[];
    struct gdt_entry *e = &gdt_entries[GDT_ENTRY_PERCPU];

    e->base_low = base & 0xFFFF;
    e->base_mid = (base >> 16) & 0xFF;
    e->base_high = (base >> 24) & 0xFF;

    asm volatile("mfence" ::: "memory");

    asm volatile("mov %[sel], %%ax\n\tmov %%ax, %%gs" :: [sel] "i"(__KERNEL_GS) : "ax");
}
#endif

static void *alloc_percpu_area(int pages)
{
    void *p = get_zeroed_pages(pages, 0);
    if (!p)
        panic("Failed to allocate per-CPU area of %lu pages\n", pages);
    return p;
}

void percpu_mem_init(void)
{
    int cpu_count = boot_info.ncpus;
    size_t mem_region_size = (size_t)((uintptr_t)&_percpu_end - (uintptr_t)&_percpu_start);
    if (mem_region_size == 0 || cpu_count <= 0)
        panic("No per-CPU data or invalid CPU count: ncpu=%d\n", cpu_count);

    size_t alloc_size = PAGE_ROUND_UP(mem_region_size);

    for (int cpu = 1; cpu < cpu_count; cpu++) {
        void *area = alloc_percpu_area(alloc_size / PAGE_SIZE);

        memcpy(area, &_percpu_start, mem_region_size);

        if (alloc_size > mem_region_size)
            memset((char *)area + mem_region_size, 0, alloc_size - mem_region_size);

        /* set the self-pointer at offset 0 so mov %gs:0 returns 'area' */
        *((uintptr_t *)area) = (uintptr_t)area;

        __per_cpu_offset[cpu] = (uintptr_t)area - (uintptr_t)&_percpu_start;
        klog(LOG_DEBUG, "Per-CPU area for CPU %d: %p (offset %lx)\n",
             cpu, area, __per_cpu_offset[cpu]);
    }
    unmap_pages(&_percpu_start, alloc_size / PAGE_SIZE);
}

void percpu_bsp_mem_init(void)
{
    size_t mem_region_size = (size_t)((uintptr_t)&_percpu_end - (uintptr_t)&_percpu_start);
    if (mem_region_size <= 0)
        panic("No per-CPU data: percpu mem size: %lu\n", mem_region_size);

    size_t alloc_size = PAGE_ROUND_UP(mem_region_size);

    void *area = alloc_percpu_area(alloc_size / PAGE_SIZE);

    memcpy(area, &_percpu_start, mem_region_size);

    if (alloc_size > mem_region_size)
        memset((char *)area + mem_region_size, 0, alloc_size - mem_region_size);

    /* set the self-pointer at offset 0 so mov %gs:0 returns 'area' */
    *((uintptr_t *)area) = (uintptr_t)area;

    __per_cpu_offset[0] = (uintptr_t)area - (uintptr_t)&_percpu_start;
    klog(LOG_DEBUG, "Per-CPU area for CPU 0: %p (offset %lx)\n",
            area, __per_cpu_offset[0]);
    percpu_init_cpu();
}

void percpu_init_cpu(void)
{
    u8 cpu_id = get_lapic_id();
    uintptr_t base = (uintptr_t)&_percpu_start + per_cpu_offset(cpu_id);
#ifdef __x86_64__
    set_gs_base(base);
#else
    set_gs_base_32((uint32_t)base);
#endif

    struct cpu_local *local = (struct cpu_local *)base;
    local->id = cpu_id;
    local->priv = get_tss();

    klog(LOG_DEBUG, "Initialized cpu local struct for CPU %d at %p\n", cpu_id, (void*)base);
}
