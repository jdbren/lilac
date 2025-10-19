#include <lilac/lilac.h>
#include <lilac/boot.h>
#include <lilac/keyboard.h>
#include <lilac/timer.h>
#include <drivers/framebuffer.h>
#include <lilac/kmm.h>
#include <asm/cpu-flags.h>
#include <asm/msr.h>
#include <asm/segments.h>

#include "gdt.h"
#include "idt.h"
#include "paging.h"
#include "apic.h"
#include "timer.h"

#define STACK_CHK_GUARD 0x595e9fbd94fda766

uintptr_t __stack_chk_guard = STACK_CHK_GUARD;
__noreturn void __stack_chk_fail(void)
{
    kerror("Stack smashing detected\n");
}

struct multiboot_info mbd;
struct acpi_info acpi;
//static struct efi_info efi;

// need to set up
uintptr_t page_directory;

extern u32 mbinfo; // defined in boot asm

void syscall_init(void)
{
    extern void syscall_entry();
    write_msr(IA32_STAR, 0, __KERNEL_CS | (__USER_CS32 << 16));
    write_msr(IA32_LSTAR, (u32)(uintptr_t)syscall_entry, (u32)((uintptr_t)syscall_entry >> 32));
    write_msr(IA32_FMASK,
        X86_FLAGS_CF|X86_FLAGS_PF|X86_FLAGS_AF|
        X86_FLAGS_ZF|X86_FLAGS_SF|X86_FLAGS_TF|
        X86_FLAGS_IF|X86_FLAGS_DF|X86_FLAGS_OF|
        X86_FLAGS_IOPL|X86_FLAGS_NT|X86_FLAGS_RF|
        X86_FLAGS_AC|X86_FLAGS_ID, 0);

    uintptr_t tss_ptr = (uintptr_t)get_tss();
    write_msr(IA32_KERNEL_GS_BASE, (u32)tss_ptr, tss_ptr >> 32);
}

__noreturn __no_stack_chk
void x86_64_kernel_early(void)
{
    idt_init();
    parse_multiboot((uintptr_t)&mbinfo, &mbd);
    mm_init(mbd.efi_mmap);
    graphics_init(mbd.framebuffer);

    acpi_early((void*)mbd.new_acpi->rsdp, &acpi);
    apic_init(acpi.madt);
    keyboard_init();
    timer_init(acpi.hpet);
    ap_init(acpi.madt->core_cnt);
    acpi_early_cleanup(&acpi);
    syscall_init();

    start_kernel();

    arch_idle();
    unreachable();
}

uintptr_t get_rsdp(void)
{
    return virt_to_phys((void*)mbd.new_acpi->rsdp);
}
