// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <string.h>
#include <lilac/lilac.h>
#include <lilac/types.h>
#include <lilac/panic.h>
#include <lilac/fs.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/timer.h>
#include <lilac/elf.h>
#include <mm/kheap.h>

__no_ret __no_stack_chk
void start_kernel(void)
{
	acpi_full_init();
	scan_sys_bus();

	fs_init();

	char buf[512];
	char buf2[512];
	char *p = buf;
	char *str = " Hello, World!\n";
	memset(buf, 0, 512);
	memset(buf2, 0, 512);
	int fd = open("/root/test", 0, 0);
	read(fd, buf, 6);
	while (*p)
		putchar(*p++);
	putchar('\n');
	p = buf;
	read(fd, buf, 4);
	while (*p)
		putchar(*p++);
	// *p = '\n';
	// strcpy(p, str);
	// printf("\n%s", buf);
	// write(fd, buf, 512);

	// read(fd, buf2, 512);
	// p = buf2;
	// while (*p)
	// 	putchar(*p++);

	//arch_idle();

    sched_init();
	arch_enable_interrupts();
	sched_clock_init();

	arch_idle();
	__builtin_unreachable();
}
