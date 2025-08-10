// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/lilac.h>
#include <lilac/boot.h>
#include <lilac/keyboard.h>
#include <lilac/timer.h>
#include <mm/kmm.h>
#include <drivers/framebuffer.h>

#include "apic.h"
#include "gdt.h"
#include "idt.h"
#include "timer.h"

#if UINT32_MAX == UINTPTR_MAX
#define STACK_CHK_GUARD 0xe2dee396
#else
#define STACK_CHK_GUARD 0x595e9fbd94fda766
#endif

uintptr_t __stack_chk_guard = STACK_CHK_GUARD;

struct multiboot_info mbd;
struct acpi_info acpi;
//static struct efi_info efi;

extern u32 mbinfo; // defined in boot asm

__no_stack_chk
void kernel_early(void)
{
    gdt_init();
    idt_init();
    parse_multiboot((uintptr_t)&mbinfo, &mbd);
    mm_init(mbd.efi_mmap);
    graphics_init(mbd.framebuffer);

    acpi_early((void*)mbd.new_acpi->rsdp, &acpi);
    apic_init(acpi.madt);
    keyboard_init();
    timer_init(1, acpi.hpet); // 1ms interval
    // ap_init(acpi.madt->core_cnt);
    acpi_early_cleanup(&acpi);

    start_kernel();
}

uintptr_t get_rsdp(void)
{
    return virt_to_phys((void*)mbd.new_acpi->rsdp);
}


[[noreturn]] void __stack_chk_fail(void)
{
#if __STDC_HOSTED__
    abort();
#else
    kerror("Stack smashing detected\n");
#endif
}
