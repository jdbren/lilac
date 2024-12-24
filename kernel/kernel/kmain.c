// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <string.h>
#include <lilac/lilac.h>
#include <lilac/types.h>
#include <lilac/log.h>
#include <lilac/panic.h>
#include <lilac/fs.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/timer.h>
#include <lilac/elf.h>
#include <lilac/tty.h>
#include <lilac/console.h>
#include <mm/kmalloc.h>

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
    sched_clock_init();

    idle();
    __builtin_unreachable();
}
