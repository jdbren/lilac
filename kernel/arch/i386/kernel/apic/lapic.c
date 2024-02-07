#include <stdbool.h>
#include <kernel/types.h>
#include <kernel/panic.h>
#include <asm/cpuid.h>
#include <asm/msr.h>
#include <paging.h>

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_MSR_BSP 0x100 // Processor is a BSP
#define IA32_APIC_BASE_MSR_ENABLE 0x800

static u32 local_apic_base;

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
 
void enable_apic(void) {
    if (!cpuHasMSR())
        kerror("CPU does not support MSRs\n");
    if (!check_apic())
        kerror("CPU does not support APIC\n");
    /* Section 11.4.1 of 3rd volume of Intel SDM recommends mapping the base address page as strong uncacheable for correct APIC operation. */
 
    /* Hardware enable the Local APIC if it wasn't enabled */
    cpu_set_apic_base(cpu_get_apic_base());

    map_page((void*)local_apic_base, (void*)local_apic_base, 0x13);
 
    printf("Local APIC Base: %x\n", local_apic_base);
    printf("Local APIC Version: %x\n", read_reg(0x30));
    printf("Spurious Int Vec: %x\n", read_reg(0xf0));
    
    /* Set the Spurious Interrupt Vector Register bit 8 to start receiving interrupts */
    //write_reg(0xF0, read_reg(0xF0) | 0x100);
}

void apic_eoi(void)
{
    write_reg(0xB0, 0);
}
