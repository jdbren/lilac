#include <lilac/lilac.h>
#include <lilac/timer.h>
#include <drivers/framebuffer.h>
#include <mm/kmm.h>
#include <lilac/boot.h>
#include <lilac/percpu.h>

#include "cpu-features.h"
#include "apic.h"

// Additional setup after mm and boot info ready
void arch_setup(void)
{
    apic_init(boot_info.acpi.madt);
    percpu_mem_init();
}

void print_system_info(void)
{
    graphics_setcolor(RGB_MAGENTA, RGB_BLACK);
    puts("LilacOS v"KERNEL_VERSION" (on "KERNEL_ARCH")\n");

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

    size_t mem_sz_mb = boot_info.total_mem_kb / 1024;
    printf("Total Memory: %u MB\n", (unsigned int)mem_sz_mb);

    u64 sys_time_ms = get_sys_time() / 1000000;
    printf("System clock running for %llu ms\n", sys_time_ms);
    struct timestamp ts = get_timestamp();
    printf("Current time: " TIME_FORMAT "\n\n",
        ts.year, ts.month, ts.day, ts.hour, ts.minute, ts.second);
    graphics_setcolor(RGB_WHITE, RGB_BLACK);
}
