#include <kernel/kmain.h>
#include <kernel/types.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/elf.h>
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

	create_process();

    while (1)
        asm("hlt");
}
