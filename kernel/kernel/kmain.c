// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/lilac.h>
#include <lilac/types.h>
#include <lilac/panic.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/timer.h>
#include <lilac/elf.h>
#include <mm/kheap.h>

void start_kernel(void)
{
    // int fd = open("A:/bin/code", 0, 0);
	// int *ptr = kmalloc(0x1000);
	// read(fd, ptr, 0x1000);
	// printf("%x\n", *ptr);
	// void *jmp = elf32_load(ptr);
	// printf("Page directory entry 768: %x\n", ((u32*)0xFFFFF000)[768]);
	// jump_usermode((u32)jmp);

	acpi_full_init();
	scan_sys_bus();

	// struct task *task = create_process("A:/bin/init");
	// schedule_task(task);

    sched_init();
	arch_enable_interrupts();

	sched_clock_init();

	arch_idle();
}
