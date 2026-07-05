#include <lilac/interrupt.h>
#include <lilac/percpu.h>
#include <asm/apic.h>
#include <asm/msr.h>

#define APIC_ICR_SH_SELF (1 << 18)
#define APIC_ICR_SH_ALL_EXCL_SELF (3 << 18)
#define APIC_ICR_SH_ALL (2 << 18)

static inline bool ipi_delivered()
{
    return !(apic_read_reg(APIC_ICR_LOW) & APIC_DELIV_STATUS);
}

static void apic_send_ipi(u8 apic_id, u8 vector, int delivery_mode, int dest_mode)
{
    apic_write_reg(APIC_ICR_HIGH, ((u32)apic_id << 24));
    apic_write_reg(APIC_ICR_LOW, vector | delivery_mode | dest_mode);
    while (!ipi_delivered())
        __pause();
}

void arch_eoi(void)
{
    apic_eoi();
}

void arch_send_ipi(u8 cpu, u8 vector)
{
    u8 apic_id = per_cpu_ptr(&cpu_local_storage, cpu)->lapic_id;
    apic_send_ipi(apic_id, vector, APIC_DELIV_FIXED, 0);
}

void arch_broadcast_others_ipi(u8 vector)
{
    apic_send_ipi(0, vector, APIC_DELIV_FIXED, APIC_ICR_SH_ALL_EXCL_SELF);
}

void arch_broadcast_all_ipi(u8 vector)
{
    apic_send_ipi(0, vector, APIC_DELIV_FIXED, APIC_ICR_SH_ALL);
}

void arch_ipi_send_self(u8 vector)
{
    apic_send_ipi(0, vector, APIC_DELIV_FIXED, APIC_ICR_SH_SELF);
}

void arch_send_nmi(u8 cpu)
{
    u8 apic_id = per_cpu_ptr(&cpu_local_storage, cpu)->lapic_id;
    apic_send_ipi(apic_id, 0, APIC_DELIV_NMI, 0);
}

void arch_broadcast_nmi_others(void)
{
    apic_send_ipi(0, 0, APIC_DELIV_NMI, APIC_ICR_SH_ALL_EXCL_SELF);
}
