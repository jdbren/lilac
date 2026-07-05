// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <asm/apic.h>

#include <lilac/lilac.h>
#include <lilac/boot.h>
#include <lilac/timer.h>
#include <lilac/sync.h>
#include <mm/kmm.h>
#include <asm/msr.h>
#include <asm/cpu.h>
#include <asm/idt.h>

#define IA32_APIC_BASE_MSR_ENABLE 0x800

#define CPUID_FEAT_EDX_APIC (1 << 9)

uintptr_t lapic_base;
static uintptr_t lapic_addr_orig;

inline void apic_eoi(void)
{
    apic_write_reg(0xB0, 0);
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
    return edx & bit_LAPIC;
}

__maybe_unused
static bool has_x2apic(void)
{
    long eax, ebx, ecx, edx;
    __cpuid(1, eax, ebx, ecx, edx);
    return ecx & bit_X2APIC;
}

/* Set the physical address for local APIC registers */
static void cpu_set_apic_base(uintptr_t apic, bool x2apic)
{
    u32 edx = (apic >> 32);
    u32 eax = (apic & 0xfffff0000) | IA32_APIC_BASE_MSR_ENABLE;
    if (x2apic)
        eax |= 0x400;
    write_msr(IA32_APIC_BASE, eax, edx);
}

static inline unsigned int cpuid_get_crystal_clock(void)
{
    u32 a, b, c, d;
    cpuid(0x15, &a, &b, &c, &d);
    return c;
}

static unsigned long apic_get_timer_hz(void)
{
    unsigned int crystal_clock = cpuid_get_crystal_clock();
    if (crystal_clock > 0)
        return crystal_clock;

    // Manually calculate the bus frequency
    unsigned long ticks_in_10ms = 0;
    apic_write_reg(APIC_TIMER_DIV, 0b1011);
    apic_write_reg(APIC_LVT_TIMER, APIC_LVT_MASKED);
    apic_write_reg(APIC_TIMER_INIT, 0xFFFFFFFF); // Set the timer to the maximum value
    busy_wait_usec(10000);
    ticks_in_10ms = 0xFFFFFFFF - apic_read_reg(APIC_TIMER_CURR); // Read the current timer value
    return (ticks_in_10ms * 100); // Convert to Hz (10ms)
}


void tsc_deadline_set(u64 deadline)
{
    write_msr(IA32_TSC_DEADLINE, (u32)(deadline & 0xFFFFFFFF), (u32)(deadline >> 32));
}

void apic_tsc_deadline(void)
{
    u32 hi, lo;
    apic_write_reg(APIC_LVT_TIMER, APIC_TIMER_DEADLINE | 0x20);
    __asm__ ("mfence");

    read_msr(IA32_TSC, &lo, &hi);
    u64 current_tsc = ((u64)hi << 32) | lo;

    tsc_deadline_set(current_tsc + 1000000);
    klog(LOG_INFO, "APIC TSC deadline timer enabled\n");
}

void apic_periodic(u32 ms)
{
    if (ms < 1 || ms > 1000)
        kerror("Invalid apic timer interval\n");
    unsigned long lapic_timer_hz = apic_get_timer_hz();
    klog(LOG_INFO, "APIC timer frequency: %lu Hz\n", lapic_timer_hz);
    apic_write_reg(APIC_LVT_TIMER, 0x20 | APIC_TIMER_PERIODIC);
    apic_write_reg(APIC_TIMER_DIV, 0b1011); // Divide by 1
    apic_write_reg(APIC_TIMER_INIT, lapic_timer_hz / (1000 / ms));
    klog(LOG_INFO, "APIC periodic timer enabled at %u ms (%lu Hz)\n", ms, lapic_timer_hz / (1000 / ms));
}

void lapic_enable(uintptr_t addr) {
    if (!check_apic())
        kerror("CPU does not support APIC\n");

    cpu_set_apic_base(addr, false);
    lapic_addr_orig = addr;

    lapic_base = (uintptr_t)map_phys((void*)addr, 0x1000,
        MEM_PF_UC | MEM_PF_WRITE | MEM_PF_READ | MEM_PF_NO_EXEC);

    klog(LOG_DEBUG, "Local APIC mapped at %p\n", (void*)lapic_base);

    /* Set the Spurious Interrupt Vector Register bit 8 to start receiving interrupts */
    apic_write_reg(APIC_REG_SPUR, 0xff | 0x100);

    kstatus(STATUS_OK, "BSP local APIC enabled\n");
}

void ap_lapic_enable(void)
{
    cpu_set_apic_base(lapic_addr_orig, false);
    apic_write_reg(APIC_REG_SPUR, 0xff | 0x100);
}


volatile atomic_int aprunning = 0;
volatile int bspdone = 0;

int ap_init(void)
{
    int ncpus = boot_info.ncpus;
    u8 bspid;
    int timeout = 100000;
    extern void ap_tramp(void);

    if (ncpus == 1)
        return 0;

    bspid = get_lapic_id();
    klog(LOG_INFO, "AP init: BSP ID %d, waking %d APs\n", bspid, ncpus - 1);
    // copy the AP trampoline code to a fixed address in low memory

    map_to_self((void*)0x8000, PAGE_SIZE, MEM_PF_WRITE | MEM_PF_READ);
    memcpy((void*)0x8000, (void*)ap_tramp, PAGE_SIZE);

    const uintptr_t base = lapic_base;
    volatile u32 *const ap_select = (volatile u32* const)(base + APIC_ICR_HIGH);
    volatile u32 *const ipi_data = (volatile u32* const)(base + APIC_ICR_LOW);

    arch_enable_interrupts();
    struct lapic_entry *lapic = boot_info.acpi.madt->lapics;
    while (lapic) {
        int running = atomic_load(&aprunning);
        int id = lapic->apic_id;
        if (id == bspid)
            goto next;

        klog(LOG_DEBUG, "Waking CPU %d...\n", id);

        // send INIT IPI
        *((volatile u32*)(base + 0x280)) = 0;               // clear errors
        *ap_select = (*ap_select & 0x00ffffff) | (id << 24); // select AP
        *ipi_data = (*ipi_data & 0xfff00000) | 0x00C500;    // INIT IPI

        timeout = 100000;
        do {
            __pause();
        } while ((*ipi_data & (1 << 12)) && timeout--);
        if (timeout <= 0)
            klog(LOG_WARN, "INIT IPI delivery stuck for CPU %d\n", id);

        busy_wait_usec(10000);                                      // wait 10ms

        // send STARTUP IPI
        *((volatile u32*)(base + 0x280)) = 0;
        for (int j = 0; j < 2; j++) {
            *ap_select = (*ap_select & 0x00ffffff) | (id << 24);
            *ipi_data = (*ipi_data & 0xfff0f800) | 0x000608;    // STARTUP IPI
            busy_wait_usec(200);                                // wait 200 usec

            timeout = 100000;
            while ((*ipi_data & (1 << 12)) && timeout--)
                __pause();
            if (running + 1 == atomic_load(&aprunning))
                break; // AP started
            if (timeout <= 0) klog(LOG_WARN, "SIPI %d delivery stuck for CPU %d\n", j+1, id);
        }
next:
        lapic = lapic->next;
    }

    arch_disable_interrupts();

    klog(LOG_DEBUG, "Waiting for APs to report...\n");
    asm volatile ("mfence" ::: "memory");
    timeout = 5000000;
    while (atomic_load(&aprunning) < ncpus - 1 && timeout--)
        __pause();
    bspdone = 1;

    kstatus(STATUS_OK, "APs running: %d\n", aprunning);
    unmap_from_self((void*)0x8000, PAGE_SIZE);
    if (aprunning == ncpus - 1)
        return 0;
    else
        return 1;
}
