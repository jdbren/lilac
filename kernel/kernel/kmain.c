// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/lilac.h>
#include <lilac/boot.h>
#include <mm/kmm.h>
#include <lilac/percpu.h>
#include <lilac/fs.h>
#include <lilac/sched.h>
#include <lilac/console.h>
#include <lilac/tty.h>
#include <drivers/keyboard.h>
#include <lilac/timer.h>
#include <drivers/framebuffer.h>
#include <acpi/acpi.h>
#include <lib/icxxabi.h>

extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

void init_ctors(void)
{
    size_t count = (uintptr_t)__init_array_end - (uintptr_t)__init_array_start;
    count /= sizeof(void*);

    for (size_t i = 0; i < count; i++) {
        __init_array_start[i]();
    }
}

__noreturn __no_stack_chk
void start_kernel(void)
{
    mm_init();
    graphics_init();

    init_ctors();
    percpu_bsp_mem_init();
    acpi_early_init();
    percpu_mem_init();
    arch_setup();

    kbd_int_init();
    timer_init();
    syscall_init();

    acpi_early_cleanup();
    acpi_subsystem_init();
    scan_sys_bus();
    arch_enable_interrupts();

    fs_init();
    sched_init();
    fb_init();
    kbd_init();
    console_init();
    tty_init();
    sched_clock_init();

    kstatus(STATUS_OK, "Kernel initialized\n");

    idle();
    unreachable();
}
