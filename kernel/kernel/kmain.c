// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/lilac.h>
#include <lilac/fs.h>
#include <lilac/sched.h>
#include <lilac/console.h>
#include <lilac/tty.h>
#include <acpi/acpi.h>
#include <lib/icxxabi.h>

extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

void _init_ctors(void)
{
    // Get the count of constructors
    size_t count = (uintptr_t)__init_array_end - (uintptr_t)__init_array_start;
    count /= sizeof(void*);

    for (size_t i = 0; i < count; i++) {
        __init_array_start[i]();
    }
}

__noreturn __no_stack_chk
void start_kernel(void)
{
    kstatus(STATUS_OK, "Starting kernel\n");
    _init_ctors();

    acpi_full_init();
    scan_sys_bus();

    fs_init();
    sched_init();

    arch_enable_interrupts();
    console_init();
    tty_init();
    sched_clock_init();

    idle();
    unreachable();
}
