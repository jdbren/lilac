// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <stdbool.h>
#include <string.h>
#include <lilac/types.h>
#include <lilac/log.h>
#include <lilac/panic.h>
#include <lilac/timer.h>
#include "mycpuid.h"
#include "msr.h"
#include "paging.h"
#include "timer.h"
#include "idt.h"
#include "apic.h"

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_MSR_BSP 0x100
#define IA32_APIC_BASE_MSR_ENABLE 0x800

#define ICR_SELECT 0x310
#define ICR_DATA 0x300


u32 lapic_base;

static inline void write_reg(u32 reg, u32 value)
{
    *(u32*)(lapic_base + reg) = value;
}

static inline u32 read_reg(u32 reg)
{
    return *(u32*)(lapic_base + reg);
}

inline void apic_eoi(void)
{
    write_reg(0xB0, 0);
}

static inline u8 get_lapic_id(void)
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
    u32 eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    return edx & CPUID_FEAT_EDX_APIC;
}

/* Set the physical address for local APIC registers */
static void cpu_set_apic_base(uintptr_t apic)
{
    lapic_base = apic;
    u32 edx = 0;
    u32 eax = (apic & 0xfffff0000) | IA32_APIC_BASE_MSR_ENABLE;

#ifdef __PHYSICAL_MEMORY_EXTENSION__
    edx = (apic >> 32) & 0x0f;
#endif
    if (cpuHasMSR())
        cpuSetMSR(IA32_APIC_BASE_MSR, eax, edx);
}

void lapic_enable(uintptr_t addr) {
    if (!check_apic())
        kerror("CPU does not support APIC\n");

    cpu_set_apic_base(addr);

    map_page((void*)lapic_base, (void*)lapic_base, PG_CACHE_DISABLE | PG_WRITE);

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

    bspid = get_lapic_id();
    // copy the AP trampoline code to a fixed address in low memory
    memcpy((void*)0x8000, (void*)ap_tramp, 4096);

    const u32 base = lapic_base;
    volatile u32 *const ap_select = (volatile u32* const)(base + ICR_SELECT);
    volatile u32 *const ipi_data = (volatile u32* const)(base + ICR_DATA);

    for (int i = 0; i < numcores; i++) {
        if (i == bspid)
            continue;

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
        *ap_select = (*ap_select & 0x00ffffff) | (i << 24);
        *ipi_data = (*ipi_data & 0xfff0f800) | 0x000608;    // STARTUP IPI
        usleep(200);                                        // wait 200 usec?

        do {
            asm volatile ("pause" : : : "memory");
        } while (*ipi_data & (1 << 12));
    }

    // release the AP spinlocks
    bspdone = 1;
    sleep(10);

    klog(LOG_INFO, "APs running: %d\n", aprunning);
    if (aprunning == numcores - 1)
        return 0;
    else
        return 1;
}

// We are still without paging here
__noreturn
void ap_startup(int id)
{
    while (1)
        asm volatile ("hlt");
}
