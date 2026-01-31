#include <lilac/lilac.h>
#include <lilac/boot.h>
#include <lilac/percpu.h>
#include <lilac/timer.h>
#include <lilac/sched.h>
#include <mm/kmm.h>
#include <mm/page.h>

#include <asm/segments.h>
#include <asm/msr.h>
#include <asm/cpu-flags.h>
#include <asm/idt.h>
#include <asm/gdt.h>
#include <asm/apic.h>
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
unsigned char ap_stack[__KERNEL_STACK_SZ * CONFIG_MAX_CPUS];

extern volatile int bspdone;
extern volatile int aprunning;

#ifndef __x86_64__
void syscall_init(void) {}
#endif

#if defined __i386__
static struct gdt_ptr ap_gdt_ptrs[CONFIG_MAX_CPUS];
static struct gdt_entry ap_gdt_entries[CONFIG_MAX_CPUS-1][GDT_NUM_ENTRIES];
extern struct gdt_entry gdt_entries[GDT_NUM_ENTRIES];

// On x86, we need to set up a separate GDT for each CPU to hold
// the TSS and per-CPU segment
void ap_gdt_init(int cpu)
{
    struct gdt_entry *entry = ap_gdt_entries[cpu - 1];
    memcpy(entry, &gdt_entries, sizeof(gdt_entries));

    struct gdt_entry *tss_entry = &entry[GDT_ENTRY_TSS];
    struct tss *tss = get_tss(cpu);
    tss->ss0 = __KERNEL_DS;
    tss->esp0 = (uintptr_t)ap_stack + __KERNEL_STACK_SZ * (cpu + 1);
    tss_entry->base_low = (u32)tss & 0xFFFF;
    tss_entry->base_mid = ((u32)tss >> 16) & 0xFF;
    tss_entry->base_high = ((u32)tss >> 24) & 0xFF;
    tss_entry->access = 0x89; // present, ring 0, type 9 (available 32-bit TSS)

    struct gdt_ptr *gdtp = &ap_gdt_ptrs[cpu - 1];
    gdtp->limit = sizeof(gdt_entries) - 1;
    gdtp->base = (void *)&ap_gdt_entries[cpu - 1];
    load_gdt(gdtp);
    load_tr(__TSS);
}
#else
// On x64 we get KERNEL_GS_BASE via swapgs, so no per-CPU GDT is needed
void ap_gdt_init(int cpu)
{
    tss_init(cpu);
}
#endif


__noreturn
void ap_startup(int id)
{
    while (!bspdone)
        __pause();

    load_idt();
    ap_gdt_init(id);
    percpu_init_cpu(id);
    set_tss_esp0((uintptr_t)ap_stack + __KERNEL_STACK_SZ * (id+1));

    klog(LOG_DEBUG, "CPU %d (LAPIC ID %d) starting up\n", id, get_lapic_id());

    ap_lapic_enable();
    syscall_init();
    sched_ap_rq_init(id);
    timer_tick_init();

    klog(LOG_INFO, "CPU %d is online\n", id);

    long int_count = 0;
    while (1) {
        __asm__ ("sti");
        __asm__ ("hlt");
    }
}

#ifndef __x86_64__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
void set_gs_base_32(u32 base)
{
    volatile struct gdt_ptr gdtp;
    store_gdt(&gdtp);

    struct gdt_entry *e = &gdtp.base[GDT_ENTRY_PERCPU];

    e->base_low = base & 0xFFFF;
    e->base_mid = (base >> 16) & 0xFF;
    e->base_high = (base >> 24) & 0xFF;

    asm volatile("mfence" ::: "memory");
    asm volatile("mov %0, %%gs" :: "r"(__KERNEL_GS));
}
#pragma GCC diagnostic pop
#endif

static void *alloc_percpu_area(int pages)
{
    void *p = get_zeroed_pages(pages, 0);
    if (!p)
        panic("Failed to allocate per-CPU area of %lu pages\n", pages);
    return p;
}

#ifdef CONFIG_SMP
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
    percpu_init_cpu(0);
}

#else

void percpu_bsp_mem_init(void)
{
    __per_cpu_offset[0] = 0;
    klog(LOG_DEBUG, "Per-CPU area for CPU 0: %p (offset %lx)\n", &_percpu_start, __per_cpu_offset[0]);
    percpu_init_cpu(0);
}

void percpu_mem_init(void) {}

#endif

void percpu_init_cpu(int cpu_id)
{
    u8 lapicid = get_lapic_id();
    uintptr_t base = (uintptr_t)&_percpu_start + per_cpu_offset(cpu_id);
#ifdef __x86_64__
    set_gs_base(base);
#else
    set_gs_base_32((uint32_t)base);
#endif

    struct cpu_local *local = (struct cpu_local *)base;
    local->id = cpu_id;
    local->lapic_id = lapicid;
    local->priv = get_tss(cpu_id);

    klog(LOG_DEBUG, "Initialized cpu local struct for CPU %d at %p\n", cpu_id, (void*)base);
}
