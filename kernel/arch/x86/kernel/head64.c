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

static void syscall_init(void)
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
    timer_init(1, acpi.hpet);
    // ap_init(acpi.madt->core_cnt);
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

void print_system_info(void)
{
    graphics_setcolor(RGB_MAGENTA, RGB_BLACK);
    puts("LilacOS v0.1.0\n\n");

    graphics_setcolor(RGB_CYAN, RGB_BLACK);
    unsigned int regs[12];
    char str[sizeof(regs) + 1];
    memset(str, 0, sizeof(str));

    __cpuid(0, regs[0], regs[1], regs[2], regs[3]);
    memcpy(str, &regs[1], 4);
    memcpy(str + 4, &regs[3], 4);
    memcpy(str + 8, &regs[2], 4);
    str[12] = '\0';
    printf("CPU Vendor: %s, ", str);

    __cpuid(0x80000000, regs[0], regs[1], regs[2], regs[3]);

    if (regs[0] < 0x80000004)
        return;

    __cpuid(0x80000002, regs[0], regs[1], regs[2], regs[3]);
    __cpuid(0x80000003, regs[4], regs[5], regs[6], regs[7]);
    __cpuid(0x80000004, regs[8], regs[9], regs[10], regs[11]);

    memcpy(str, regs, sizeof(regs));
    str[sizeof(regs)] = '\0';
    printf("%s\n", str);

    __cpuid(0x80000005, regs[0], regs[1], regs[2], regs[3]);
    printf("Cache line: %u bytes, ", regs[2] & 0xff);
    printf("L1 Cache: %u KB, ", regs[2] >> 24);

    __cpuid(0x80000006, regs[0], regs[1], regs[2], regs[3]);
    printf("L2 Cache: %u KB\n", regs[2] >> 16);

    size_t mem_sz_mb = arch_get_mem_sz() / 0x400;
    printf("Memory: %u MB\n", mem_sz_mb);

    u64 sys_time_ms = get_sys_time() / 1000000;
    printf("System clock running for %llu ms\n", sys_time_ms);
    struct timestamp ts = get_timestamp();
    printf("Current time: " TIME_FORMAT "\n\n",
        ts.year, ts.month, ts.day, ts.hour, ts.minute, ts.second);
    graphics_setcolor(RGB_WHITE, RGB_BLACK);
}
