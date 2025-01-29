// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/lilac.h>
#include <lilac/fs.h>
#include <lilac/sched.h>
#include <lilac/console.h>
#include <acpi/acpi.h>

__no_ret __no_stack_chk
void start_kernel(void)
{
    kstatus(STATUS_OK, "Starting kernel\n");

    acpi_full_init();
    scan_sys_bus();

    fs_init();
    sched_init();

    arch_enable_interrupts();
    console_init();
    log_init(LOG_DEBUG);
    sched_clock_init(); // kernel is live, will jmp to init task

    idle();
    __builtin_unreachable();
}
