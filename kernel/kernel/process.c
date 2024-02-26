// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <stdatomic.h>
#include <string.h>
#include <kernel/process.h>
#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/panic.h>
#include <kernel/elf.h>
#include <kernel/sched.h>
#include <mm/kheap.h>
#include <mm/asm.h>
#include <fs/vfs.h>
#include "timer.h"

#define MAX_TASKS 1024
#define INIT_STACK(KSTACK) ((u32)KSTACK + __KERNEL_STACK_SZ - sizeof(size_t))
#define current get_current_task()

static struct task tasks[MAX_TASKS];
static int num_tasks;

void idle(void)
{
    printf("Idle process started\n");
    while (1)
        asm("hlt");
}

void start_process(void)
{
    int i = 1;
    printf("Process %d started\n", current->pid);

    char *path = current->info->path;
    int fd = open(path, 0, 0);
    if (fd < 0) {
        printf("Failed to open file %s\n", path);
        return;
    }

    struct elf_header *hdr = kzmalloc(0x1000);
    int bytes = read(fd, hdr, 0x1000);
    if (hdr->sig != ELF_MAGIC) {
        printf("Invalid ELF signature\n");
        return;
    }
    else {
        printf("Read %d bytes from %s\n", bytes, path);
    }
    void *jmp = elf32_load(hdr);
    arch_user_stack();
    asm("cli");
    jump_usermode((u32)jmp, __USER_STACK - 4);


    while (1) {
        //printf("Process %d running for %d time\n", current->pid, i++);
        //sleep(500);
        asm("hlt");
    }
}

struct task* create_process(const char *path)
{
    static int pid = 1;
    struct task *new_task = &tasks[++pid];
    // Allocate new page directory
    struct mm_info mem = arch_process_mmap();

    memcpy(new_task->name, path, strlen(path) + 1);
    new_task->pid = pid;
    new_task->ppid = current->pid;
    new_task->pgd = mem.pgd;
    new_task->pc = (u32)(start_process);
    new_task->stack = (void*)INIT_STACK(mem.kstack);
    new_task->state = 0;
    new_task->info = kzmalloc(sizeof(*new_task->info));
    new_task->info->path = path;

    num_tasks++;
    return new_task;
}

struct task* init_process(void)
{
    num_tasks = 1;
    struct task *this = &tasks[1];

    this->pid = 1;
    this->pgd = arch_get_pgd();
    printf("Page directory: %x\n", this->pgd);
    this->ppid = 0;
    this->priority = 0;
    memcpy(this->name, "_init", 6);

    return this;
}
