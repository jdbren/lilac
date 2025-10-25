#include <lilac/lilac.h>
#include <lilac/boot.h>
#include <lilac/percpu.h>
#include <lilac/kmm.h>

#include <asm/segments.h>
#include <asm/msr.h>
#include <asm/cpu-flags.h>

#include "idt.h"
#include "gdt.h"
#include "apic.h"
#include "timer.h"

static inline void set_kernel_gs_base(u64 base)
{
    asm volatile (
        "wrmsr" :: "c"(IA32_KERNEL_GS_BASE), "a"((u32)base), "d"((u32)(base >> 32))
    );
}

int ncpus;
uintptr_t __per_cpu_offset[CONFIG_MAX_CPUS];

__section(PERCPU_SECTION".first") __used
struct cpu_local cpu_local_storage = {
    .priv = nullptr,
    .id = -1,
    .reserved = 0,
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
    klog(LOG_DEBUG, "AP %d started\n", id);
    tss_init();
    set_tss_esp0((uintptr_t)ap_stack + 0x1000 * (id+1));
    load_idt();
    ap_lapic_enable();
    syscall_init();
    // timer_tick_init();

    while (1)
        asm ("hlt");
}

extern char _percpu_start;
extern char _percpu_end;

static void *alloc_percpu_area(size_t size)
{
    void *p = kvirtual_alloc(size, PG_WRITE);
    if (!p)
        panic("Failed to allocate per-CPU area of size %lu\n", size);
    return p;
}

void percpu_mem_init(int cpu_count)
{
    ncpus = cpu_count;
    size_t mem_region_size = (size_t)((uintptr_t)&_percpu_end - (uintptr_t)&_percpu_start);
    if (mem_region_size == 0 || cpu_count <= 0)
        panic("No per-CPU data or invalid CPU count: ncpu=%d\n", cpu_count);

    size_t alloc_size = PAGE_ROUND_UP(mem_region_size);

    for (int cpu = 0; cpu < cpu_count; cpu++) {
        void *area = alloc_percpu_area(alloc_size);

        memcpy(area, &_percpu_start, mem_region_size);

        if (alloc_size > mem_region_size)
            memset((char *)area + mem_region_size, 0, alloc_size - mem_region_size);

        __per_cpu_offset[cpu] = (uintptr_t)area - (uintptr_t)&_percpu_start;
        klog(LOG_DEBUG, "Per-CPU area for CPU %d: %p (offset %lx)\n",
             cpu, area, __per_cpu_offset[cpu]);
    }

}

void percpu_init_cpu(void)
{
    u8 cpu_id = get_lapic_id();
    uintptr_t base = (uintptr_t)&_percpu_start + per_cpu_offset(cpu_id);
    set_kernel_gs_base(base);

    struct cpu_local *local = (struct cpu_local *)base;
    local->id = cpu_id;
    local->priv = get_tss();

    klog(LOG_DEBUG, "Initialized cpu local struct for CPU %d at %p\n", cpu_id, (void*)base);
}
