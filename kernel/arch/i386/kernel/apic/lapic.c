#include <stdbool.h>
#include <string.h>
#include <kernel/types.h>
#include <kernel/panic.h>
#include <asm/cpuid.h>
#include <asm/msr.h>
#include "paging.h"
#include "timer.h"
#include "idt.h"
#include "apic.h"

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_MSR_BSP 0x100 // Processor is a BSP
#define IA32_APIC_BASE_MSR_ENABLE 0x800

u32 local_apic_base;

static inline void write_reg(u32 reg, u32 value)
{
    *(u32*)(local_apic_base + reg) = value;
}

static inline u32 read_reg(u32 reg)
{
    return *(u32*)(local_apic_base + reg);
}

/**
 * returns 'true' if the CPU supports APIC
 * and if the local APIC hasn't been disabled in MSRs
 */
bool check_apic(void)
{
    u32 eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    return edx & CPUID_FEAT_EDX_APIC;
}

/* Set the physical address for local APIC registers */
static void cpu_set_apic_base(uintptr_t apic)
{
    local_apic_base = apic;
    u32 edx = 0;
    u32 eax = (apic & 0xfffff0000) | IA32_APIC_BASE_MSR_ENABLE;

#ifdef __PHYSICAL_MEMORY_EXTENSION__
    edx = (apic >> 32) & 0x0f;
#endif
    cpuSetMSR(IA32_APIC_BASE_MSR, eax, edx);
}

/**
 * Get the physical address of the APIC registers page
 */
static uintptr_t cpu_get_apic_base(void)
{
    u32 eax, edx;
    cpuGetMSR(IA32_APIC_BASE_MSR, &eax, &edx);

#ifdef __PHYSICAL_MEMORY_EXTENSION__
    return (eax & 0xfffff000) | ((edx & 0x0f) << 32);
#else
    return (eax & 0xfffff000);
#endif
}

void lapic_enable(uintptr_t addr) {
    if (!cpuHasMSR())
        kerror("CPU does not support MSRs\n");
    if (!check_apic())
        kerror("CPU does not support APIC\n");
    /* Section 11.4.1 of 3rd volume of Intel SDM recommends mapping the base address page as strong uncacheable for correct APIC operation. */

    /* Hardware enable the Local APIC if it wasn't enabled */
    cpu_set_apic_base(addr);

    map_page((void*)local_apic_base, (void*)local_apic_base, PG_CACHE_DISABLE | PG_WRITE);

    /* Set the Spurious Interrupt Vector Register bit 8 to start receiving interrupts */
    write_reg(0xF0, read_reg(0xF0) | 0x100);

    printf("BSP local APIC enabled\n");
}

void apic_eoi(void)
{
    write_reg(0xB0, 0);
}

volatile u8 aprunning = 0;  // count how many APs have started
u8 bspid, bspdone = 0;      // BSP id and spinlock flag
extern void ap_trampoline(void);

int ap_init(u8 numcores)
{
    int i;
    // get the BSP's Local APIC ID
    asm volatile ("mov $1, %%eax; cpuid; shrl $24, %%ebx;": "=b"(bspid) : : );
    // copy the AP trampoline code to a fixed address in low conventional memory (to address 0x0800:0x0000)
    memcpy((void*)0x8000, (void*)ap_trampoline, 4096);
    //idt_entry(0x40, (u32)ap_trampoline, 0x08, INT_GATE);

    volatile u8* apic_base = (volatile u8*)local_apic_base;

    for (i = 0; i < numcores; i++) {
        if (i == bspid)
            continue;

        // send INIT IPI
        *((volatile u32*)(apic_base + 0x280)) = 0;                             // clear APIC errors
        *((volatile u32*)(apic_base + 0x310)) =
            (*((volatile u32*)(apic_base + 0x310)) & 0x00ffffff) | (i << 24);  // select AP
        *((volatile u32*)(apic_base + 0x300)) =
            (*((volatile u32*)(apic_base + 0x300)) & 0xfff00000) | 0x00C500;   // trigger INIT IPI

        do {
            asm volatile ("pause" : : : "memory");
        } while (*((volatile u32*)(apic_base + 0x300)) & (1 << 12));         // wait for delivery

        *((volatile u32*)(apic_base + 0x310)) =
            (*((volatile u32*)(apic_base + 0x310)) & 0x00ffffff) | (i << 24);         // select AP
        *((volatile u32*)(apic_base + 0x300)) =
            (*((volatile u32*)(apic_base + 0x300)) & 0xfff00000) | 0x008500;          // deassert

        do {
            asm volatile ("pause" : : : "memory");
        } while (*((volatile u32*)(apic_base + 0x300)) & (1 << 12));         // wait for delivery
        sleep(10);                                                               // wait 10 msec

        // send STARTUP IPI
        *((volatile u32*)(apic_base + 0x280)) = 0;                             // clear APIC errors
        *((volatile u32*)(apic_base + 0x310)) =
            (*((volatile u32*)(apic_base + 0x310)) & 0x00ffffff) | (i << 24); // select AP
        *((volatile u32*)(apic_base + 0x300)) =
            (*((volatile u32*)(apic_base + 0x300)) & 0xfff0f800) | 0x000608;  // trigger STARTUP IPI for 0800:0000
        sleep(1);                                                             // wait 200 usec

        do {
            asm volatile ("pause" : : : "memory");
        } while (*((volatile u32*)(apic_base + 0x300)) & (1 << 12)); // wait for delivery
    }

    // release the AP spinlocks
    bspdone = 1;
    // now you'll have the number of running APs in 'aprunning'
    sleep(10);
    printf("APs running: %d\n", aprunning);
    if (aprunning == numcores - 1)
        return 0;
    else
        return 1;
}
