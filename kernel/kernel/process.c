// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/process.h>

#include <stdatomic.h>
#include <string.h>

#include <lilac/types.h>
#include <lilac/config.h>
#include <lilac/panic.h>
#include <lilac/elf.h>
#include <lilac/fs.h>
#include <lilac/sched.h>
#include <lilac/timer.h>
#include <lilac/log.h>
#include <lilac/syscall.h>
#include <lilac/mm.h>
#include <mm/kmm.h>
#include <mm/kmalloc.h>

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

u32 get_pid(void)
{
    return current->pid;
}

static void start_process(void)
{
    struct mm_info *mem = current->mm;
    klog(LOG_DEBUG, "Process %d started\n", current->pid);

    const char *path = current->info.path;
    int fd = open(path, 0, 0);
    if (fd < 0) {
        klog(LOG_ERROR, "Failed to open file %s\n", path);
        return;
    }

    struct elf_header *hdr = kzmalloc(0x1000);
    int bytes = 0;
    while(read(fd, (u8*)hdr + bytes, 0x1000) > 0) {
        bytes += 0x1000;
        hdr = krealloc(hdr, bytes + 0x1000);
    }
    klog(LOG_DEBUG, "Read %d bytes from %s\n", bytes, path);

    void *jmp = elf32_load(hdr, mem);
    if (!jmp)
        kerror("Failed to load ELF file\n");

    // close(fd);

    mem->start_stack = (uintptr_t)arch_user_stack();

    struct vm_desc *stack_desc = kzmalloc(sizeof(*stack_desc));
    stack_desc->mm = mem;
    stack_desc->start = mem->start_stack;
    stack_desc->end = __USER_STACK;
    stack_desc->vm_prot = PROT_READ | PROT_WRITE;
    stack_desc->vm_flags = MAP_PRIVATE | MAP_ANONYMOUS;
    vma_list_insert(stack_desc, &mem->mmap);

    // Set up heap
    mem->start_brk = 0x40000000UL;
    mem->brk = 0x40000000UL;

    struct vm_desc *heap_desc = kzmalloc(sizeof(*heap_desc));
    heap_desc->mm = mem;
    heap_desc->start = mem->start_brk;
    heap_desc->end = mem->brk;
    heap_desc->vm_prot = PROT_READ | PROT_WRITE;
    heap_desc->vm_flags = MAP_PRIVATE | MAP_ANONYMOUS;
    vma_list_insert(heap_desc, &mem->mmap);

    struct vm_desc *desc = mem->mmap;
    // while (desc) {
    //     klog(LOG_DEBUG, "Start: %x\n", desc->start);
    //     klog(LOG_DEBUG, "End: %x\n", desc->end);
    //     klog(LOG_DEBUG, "Prot: %x\n", desc->vm_prot);
    //     klog(LOG_DEBUG, "Flags: %x\n", desc->vm_flags);
    //     desc = desc->vm_next;
    // }
    close(fd);
    klog(LOG_DEBUG, "Going to user mode\n");
    jump_usermode((u32)jmp, __USER_STACK - 4);
}

struct task* create_process(const char *path)
{
    static int pid = 1;
    struct task *new_task = &tasks[++pid];
    // Allocate new page directory
    struct mm_info *mem = arch_process_mmap();

    memcpy(new_task->name, path, strlen(path) + 1);
    new_task->pid = pid;
    new_task->ppid = current->pid;
    new_task->parent = current;
    new_task->mm = mem;
    new_task->pgd = mem->pgd;
    new_task->pc = (u32)(start_process);
    new_task->stack = (void*)INIT_STACK(mem->kstack);
    new_task->state = TASK_SLEEPING;
    new_task->info.path = kzmalloc(strlen(path) + 1);
    memcpy(new_task->info.path, path, strlen(path) + 1);
    new_task->fs.files.fdarray = kcalloc(4, sizeof(struct file));
    new_task->fs.files.max = 4;

    num_tasks++;
    return new_task;
}

struct task *init_process(void)
{
    num_tasks = 1;
    struct task *this = &tasks[1];
    struct mm_info *mem = arch_process_mmap();

    this->pid = 1;
    this->ppid = 0;
    this->mm = mem;
    this->pgd = mem->pgd;
    this->stack = (void*)INIT_STACK(mem->kstack);
    this->pc = (u32)(start_process);
    this->state = TASK_RUNNING;
    this->fs.files.fdarray = kcalloc(4, sizeof(struct file));
    this->fs.files.max = 4;
    this->fs.cwd = kzmalloc(2);
    this->fs.cwd[0] = '/';
    this->fs.cwd[1] = '\0';
    this->info.path = "/sbin/init";
    memcpy(this->name, "init", 5);

    return this;
}

int fork(void)
{
    asm("hlt");
}
SYSCALL_DECL0(fork)

int exit(int status)
{
    current->state = TASK_DEAD;
    klog(LOG_INFO, "Process %d exited with status %d\n", current->pid, status);
    schedule();
}
SYSCALL_DECL1(exit, int, status)

int getcwd(char *buf, size_t size)
{
    if (size < strlen(current->fs.cwd) + 1)
        return -1;

    strcpy(buf, current->fs.cwd);
    return 0;
}
SYSCALL_DECL2(getcwd, char*, buf, size_t, size)
