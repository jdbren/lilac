#include <lilac/lilac.h>
#include <lilac/boot.h>

#include "idt.h"

#define STACK_CHK_GUARD 0x595e9fbd94fda766

uintptr_t __stack_chk_guard = STACK_CHK_GUARD;

struct multiboot_info mbd;
static struct acpi_info acpi;
//static struct efi_info efi;

extern u32 mbinfo; // defined in boot asm
static void parse_multiboot(uintptr_t, struct multiboot_info*);

__no_stack_chk __no_ret
void x86_64_kernel_early(void)
{
    idt_init();
    parse_multiboot((uintptr_t)&mbinfo, &mbd);

    arch_idle();
    __builtin_unreachable();
}
