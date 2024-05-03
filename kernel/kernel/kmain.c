// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/lilac.h>
#include <lilac/types.h>
#include <lilac/panic.h>
#include <lilac/fs.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/timer.h>
#include <lilac/elf.h>
#include <mm/kheap.h>

__no_ret __no_stack_chk void start_kernel(void)
{
	acpi_full_init();
	scan_sys_bus();

	fs_init();

    sched_init();
	arch_enable_interrupts();
	sched_clock_init();

	arch_idle();
	__builtin_unreachable();
}
