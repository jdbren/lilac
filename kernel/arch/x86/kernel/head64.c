#include <lilac/lilac.h>
#include <lilac/boot.h>
#include <lilac/keyboard.h>
#include <lilac/timer.h>
#include <drivers/framebuffer.h>
#include <lilac/kmm.h>
#include <lilac/percpu.h>
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

struct boot_info boot_info;
extern u32 mbinfo; // defined in boot asm

// need to set up
uintptr_t page_directory;

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
    percpu_init_cpu();
}

__noreturn __no_stack_chk
void x86_64_kernel_early(void)
{
    parse_multiboot((uintptr_t)&mbinfo);

    idt_init();
    x86_setup_mem();

    start_kernel();

    arch_idle();
    unreachable();
}

uintptr_t get_rsdp(void)
{
    return virt_to_phys((void*)boot_info.mbd.new_acpi->rsdp);
}
