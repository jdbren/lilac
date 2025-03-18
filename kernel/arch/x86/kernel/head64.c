#include <lilac/lilac.h>
#include <lilac/boot.h>
#include <lilac/keyboard.h>
#include <drivers/framebuffer.h>
#include <mm/kmm.h>

#include "idt.h"
#include "paging.h"
#include "apic.h"
#include "timer.h"

#define STACK_CHK_GUARD 0x595e9fbd94fda766

uintptr_t __stack_chk_guard = STACK_CHK_GUARD;

struct multiboot_info mbd;
static struct acpi_info acpi;
//static struct efi_info efi;

// need to set up
uintptr_t page_directory;

extern u32 mbinfo; // defined in boot asm

__no_stack_chk __no_ret
void x86_64_kernel_early(void)
{
    idt_init();
    parse_multiboot((uintptr_t)&mbinfo, &mbd);
    mm_init(mbd.efi_mmap);
    graphics_init(mbd.framebuffer);

    acpi_early((void*)mbd.new_acpi->rsdp, &acpi);
    apic_init(acpi.madt);
    keyboard_init();
    timer_init(1, acpi.hpet); // 1ms interval
    ap_init(acpi.madt->core_cnt);
    acpi_early_cleanup(&acpi);

    start_kernel();

    arch_idle();
    unreachable();
}

uintptr_t get_rsdp(void)
{
    return virt_to_phys((void*)mbd.new_acpi->rsdp);
}
