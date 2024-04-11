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
