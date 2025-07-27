// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <cpuid.h>
#include <lilac/lilac.h>
#include <lilac/timer.h>
#include <mm/kmm.h>
#include <asm/msr.h>
#include "timer.h"
#include "idt.h"
#include "apic.h"
#include "paging.h"

#define IA32_APIC_BASE_MSR_BSP 0x100
#define IA32_APIC_BASE_MSR_ENABLE 0x800

#define ICR_SELECT 0x310
#define ICR_DATA 0x300

#define CPUID_FEAT_EDX_APIC (1 << 9)

uintptr_t lapic_base;

static inline void write_reg(u32 reg, u32 value)
{
    *(volatile u32*)(lapic_base + reg) = value;
}

static inline u32 read_reg(u32 reg)
{
    return *(volatile u32*)(lapic_base + reg);
}

inline void apic_eoi(void)
{
    write_reg(0xB0, 0);
}

u8 get_lapic_id(void)
{
    u8 bspid;
    asm volatile (
        "mov $1, %%eax\n\t"
        "cpuid\n\t"
        "shrl $24, %%ebx\n\t"
        : "=b"(bspid)
        : : "eax", "ecx", "edx"
    );
    return bspid;
}

/**
 * returns 'true' if the CPU supports APIC
 * and if the local APIC hasn't been disabled in MSRs
 */
static bool check_apic(void)
{
    long eax, ebx, ecx, edx;
    __cpuid(1, eax, ebx, ecx, edx);
    return edx & CPUID_FEAT_EDX_APIC;
}

/* Set the physical address for local APIC registers */
static void cpu_set_apic_base(uintptr_t apic)
{
    u32 edx = 0;
    u32 eax = (apic & 0xfffff0000) | IA32_APIC_BASE_MSR_ENABLE;

#ifdef __PHYSICAL_MEMORY_EXTENSION__
    edx = (apic >> 32) & 0x0f;
#endif
    if (cpu_has_msr())
        write_msr(IA32_APIC_BASE, eax, edx);
}

void lapic_enable(uintptr_t addr) {
    if (!check_apic())
        kerror("CPU does not support APIC\n");

    cpu_set_apic_base(addr);

    lapic_base = (uintptr_t)map_phys((void*)addr, 0x1000, PG_STRONG_UC | PG_WRITE);

    /* Set the Spurious Interrupt Vector Register bit 8 to start receiving interrupts */
    write_reg(0xF0, read_reg(0xF0) | 0x100);

    kstatus(STATUS_OK, "BSP local APIC enabled\n");
}



volatile u8 aprunning;
u8 bspdone;

int ap_init(u8 numcores)
{
    u8 bspid;
    extern void ap_tramp(void);

    if (numcores == 1)
        return 0;

    bspid = get_lapic_id();
    // copy the AP trampoline code to a fixed address in low memory

    map_to_self((void*)0x8000, PAGE_SIZE, PG_WRITE);
    memcpy((void*)0x8000, (void*)ap_tramp, PAGE_SIZE);

    const uintptr_t base = lapic_base;
    volatile u32 *const ap_select = (volatile u32* const)(base + ICR_SELECT);
    volatile u32 *const ipi_data = (volatile u32* const)(base + ICR_DATA);

    arch_enable_interrupts();
    for (int i = 0; i < numcores; i++) {
        if (i == bspid)
            continue;
        klog(LOG_DEBUG, "Sending INIT IPI to AP %d\n", i);

        // send INIT IPI
        *((volatile u32*)(base + 0x280)) = 0;               // clear errors
        *ap_select = (*ap_select & 0x00ffffff) | (i << 24); // select AP
        *ipi_data = (*ipi_data & 0xfff00000) | 0x00C500;    // INIT IPI
        sleep(10);                                          // wait

        do {
            asm volatile ("pause" : : : "memory");
        } while (*ipi_data & (1 << 12));                    // wait for deliv

        *ap_select = (*ap_select & 0x00ffffff) | (i << 24);
        *ipi_data = (*ipi_data & 0xfff00000) | 0x008500;    // deassert

        do {
            asm volatile ("pause" : : : "memory");
        } while (*ipi_data & (1 << 12));
        sleep(10);                                          // wait

        // send STARTUP IPI
        *((volatile u32*)(base + 0x280)) = 0;
        for (int j = 0; j < 2; j++) {
            *ap_select = (*ap_select & 0x00ffffff) | (i << 24);
            *ipi_data = (*ipi_data & 0xfff0f800) | 0x000608;    // STARTUP IPI
            usleep(200);                                        // wait 200 usec?
        }

        do {
            asm volatile ("pause" : : : "memory");
        } while (*ipi_data & (1 << 12));
    }

    // release the AP spinlocks
    bspdone = 1;
    sleep(10);
    arch_disable_interrupts();

    kstatus(STATUS_OK, "APs running: %d\n", aprunning);
    unmap_from_self((void*)0x8000, PAGE_SIZE);
    if (aprunning == numcores - 1)
        return 0;
    else
        return 1;
}
