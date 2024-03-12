// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <kernel/lilac.h>
#include <kernel/types.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/sched.h>
#include <kernel/elf.h>
#include <mm/kheap.h>
#include <fs/vfs.h>
#include "timer.h"

void start_kernel(void)
{
    // int fd = open("A:/bin/code", 0, 0);
	// int *ptr = kmalloc(0x1000);
	// read(fd, ptr, 0x1000);
	// printf("%x\n", *ptr);
	// void *jmp = elf32_load(ptr);
	// printf("Page directory entry 768: %x\n", ((u32*)0xFFFFF000)[768]);
	//jump_usermode((u32)jmp);

	

	struct task *task = create_process("A:/bin/init");
	schedule_task(task);


	// struct task *task2 = create_process("3rd process");
	// schedule_task(task2);

	// struct task *task3 = create_process("4th process");
	// schedule_task(task3);

    while (1) {
		sleep(4000);
		printf("Kernel task running\n");
        asm("hlt");
		asm("cli");
	}
}
