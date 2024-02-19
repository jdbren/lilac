#include <kernel/kmain.h>
#include <kernel/types.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/sched.h>
#include <kernel/elf.h>
#include "timer.h"
#include <mm/kheap.h>
#include <fs/vfs.h>

void jump_usermode(u32 addr);

void start_kernel(void)
{
    // int fd = open("A:/bin/code", 0, 0);
	// int *ptr = kmalloc(0x1000);
	// read(fd, ptr, 0x1000);
	// printf("%x\n", *ptr);
	// void *jmp = elf32_load(ptr);
	// printf("Page directory entry 768: %x\n", ((u32*)0xFFFFF000)[768]);
	//jump_usermode((u32)jmp);

	init_sched(1000);
	
	struct task *task = create_process("2nd process");
	schedule_task(task);

	struct task *task2 = create_process("3rd process");
	schedule_task(task2);

	struct task *task3 = create_process("4th process");
	schedule_task(task3);

    while (1) {
        asm("hlt");
	}
}
