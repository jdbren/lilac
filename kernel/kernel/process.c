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

static struct task tasks[MAX_TASKS];
static int num_tasks;

u32 get_pid(void)
{
    return current->pid;
}

static void start_process(void)
{
    struct mm_info *mem = current->mm;
    klog(LOG_DEBUG, "Process %d started\n", current->pid);

    const char *path = current->info.path;
    struct file *file = vfs_open(path, 0, 0);
    if (!file) {
        klog(LOG_ERROR, "Failed to open file %s\n", path);
        return;
    }

    struct elf_header *hdr = kzmalloc(0x1000);
    int bytes = 0;
    while(vfs_read(file, (u8*)hdr + bytes, 0x1000) > 0) {
        bytes += 0x1000;
        hdr = krealloc(hdr, bytes + 0x1000);
    }

    void *jmp = elf32_load(hdr, mem);
    if (!jmp)
        kerror("Failed to load ELF file\n");

    klog(LOG_DEBUG, "ELF loaded\n");

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

    /*
    struct vm_desc *desc = mem->mmap;
    while (desc) {
        klog(LOG_DEBUG, "Start: %x\n", desc->start);
        klog(LOG_DEBUG, "End: %x\n", desc->end);
        klog(LOG_DEBUG, "Prot: %x\n", desc->vm_prot);
        klog(LOG_DEBUG, "Flags: %x\n", desc->vm_flags);
        desc = desc->vm_next;
    }
    */
    vfs_close(file);

    volatile uintptr_t *stack = (void*)(__USER_STACK - 128); // Will place argv entries here
    if (!current->info.argv) {
        *stack = 0;
        *--stack = 0;
        goto skip;
    }
    // Write args to user stack
    u32 argc = 0;
    char **argv = (char**)(stack);
    for (; current->info.argv[argc] && argc < 31; argc++) {
        u32 len = strlen(current->info.argv[argc]) + 1;
        len = (len + 3) & ~3; // Align stack
        stack = (u32*)((uintptr_t)stack - len);
        strcpy((char*)stack, current->info.argv[argc]);
        argv[argc] = (char*)stack;
    }
    argv[argc] = NULL;

    // Print the arguments
    for (u32 i = 0; i < argc; i++) {
        klog(LOG_DEBUG, "Arg %d: %s\n", i, argv[i]);
    }

    *--stack = (u32)argv;
    *--stack = argc;

skip:
    klog(LOG_DEBUG, "Going to user mode, jmp = %x\n", jmp);
    jump_usermode(jmp, stack, current->kstack);
}

/*
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
    new_task->pc = (uintptr_t)(start_process);
    new_task->kstack = (void*)INIT_STACK(mem->kstack);
    new_task->state = TASK_SLEEPING;
    new_task->info.path = kzmalloc(strlen(path) + 1);
    memcpy(new_task->info.path, path, strlen(path) + 1);
    new_task->fs.files.fdarray = kcalloc(4, sizeof(struct file));
    new_task->fs.files.max = 4;

    num_tasks++;
    return new_task;
}
*/

struct task *init_process(void)
{
    num_tasks = 1;
    struct task *this = &tasks[1];
    struct mm_info *mem = arch_process_mmap();

    this->pid = 1;
    this->ppid = 0;
    this->mm = mem;
    this->pgd = mem->pgd;
    this->kstack = (void*)INIT_STACK(mem->kstack);
    this->pc = (uintptr_t)(start_process);
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

void clone_process(struct task *parent, struct task *child)
{
    child->ppid = parent->pid;
    child->parent = parent;
    child->mm = arch_copy_mmap(parent->mm);
    child->pgd = child->mm->pgd;
    child->pc = parent->pc;
    child->kstack = (void*)INIT_STACK(child->mm->kstack);
    child->state = TASK_SLEEPING;
    child->info.path = kzmalloc(strlen(parent->info.path) + 1);
    strcpy((char*)child->info.path, parent->info.path);
    child->fs.cwd = kzmalloc(strlen(parent->fs.cwd) + 1);
    strcpy(child->fs.cwd, parent->fs.cwd);
    child->fs.files.fdarray = kcalloc(parent->fs.files.max, sizeof(struct file));
    child->fs.files.max = parent->fs.files.max;
    child->fs.files.size = parent->fs.files.size;
    for (size_t i = 0; i < parent->fs.files.size; i++) {
        child->fs.files.fdarray[i] = parent->fs.files.fdarray[i];
        child->fs.files.fdarray[i]->f_count++;
    }
    memcpy(child->name, parent->name, strlen(parent->name) + 1);
    strcat(child->name, " (clone)");
}

static void return_from_fork(void)
{
    klog(LOG_DEBUG, "Returning from fork\n");
    arch_return_from_fork(current->regs);
}

int do_fork(void)
{
    u32 pid = ++num_tasks;
    struct task *parent = current;
    struct task *child = &tasks[pid];

    child->pid = pid;
    clone_process(parent, child);
    child->pc = (uintptr_t)return_from_fork;
    child->regs = arch_copy_regs(parent->regs);
    child->state = TASK_RUNNING;
    schedule_task(child);

    return pid;
}

__noreturn void exec_and_return(void)
{
    klog(LOG_DEBUG, "Entering exec_and_return, pid = %d\n", current->pid);
    struct mm_info *mem = arch_process_remap(current->mm);
    struct task *task = current;

    task->mm = mem;
    task->pgd = mem->pgd;
    task->kstack = (void*)INIT_STACK(mem->kstack);
    task->pc = (uintptr_t)start_process;
    task->state = TASK_RUNNING;

    jump_new_proc(task);
    klog(LOG_FATAL, "exec_and_return: Should never be reached\n");
    __builtin_unreachable();
}

int do_execve(const char *path, char *const argv[], char *const envp[])
{
    struct task_info *info = &current->info;
    int i;
    struct file *file = vfs_open(path, 0, 0);

    if (!file) {
        klog(LOG_ERROR, "file %s not found\n", path);
        return -1;
    } else {
        vfs_close(file);
    }

    if (info->path)
        kfree((void*)info->path);
    info->path = kzmalloc(strlen(path) + 1);
    strcpy((char*)info->path, path);

    if (info->argv) {
        for (i = 0; info->argv[i]; i++)
            kfree(info->argv[i]);
        kfree(info->argv);
    }
    for (i = 0; argv[i]; i++) {
        klog(LOG_DEBUG, "Arg %d: %s\n", i, argv[i]);
        info->argv = krealloc(info->argv, (i + 1) * sizeof(char*));
        info->argv[i] = kzmalloc(strlen(argv[i]) + 1);
        strcpy(info->argv[i], argv[i]);
    }
    info->argv = krealloc(info->argv, (i + 1) * sizeof(char*));
    info->argv[i] = 0;

    klog(LOG_INFO, "Executing %s\n", info->path);

    exec_and_return();
    return -1;
}

SYSCALL_DECL3(execve, const char*, path, char* const*, argv, char* const*, envp)
{
    return do_execve(path, argv, envp);
}

void cleanup_task(struct task *task)
{
    klog(LOG_DEBUG, "Cleaning up task %d\n", task->pid);
    kfree((void*)task->info.path);
    for (int i = 0; task->info.argv[i]; i++)
        kfree(task->info.argv[i]);
    kfree(task->info.argv);
    kfree(task->fs.cwd);
    kfree(task->fs.files.fdarray); // TODO: File management
    struct vm_desc *desc = task->mm->mmap;
    while (desc) {
        struct vm_desc *next = desc->vm_next;
        kfree(desc);
        desc = next;
    }
    kfree(task->mm);
}

int exit(int status)
{
    current->state = TASK_DEAD;
    klog(LOG_INFO, "Process %d exited with status %d\n", current->pid, status);
    schedule();
    return 0;
}
SYSCALL_DECL1(exit, int, status)
{
    return exit(status);
}

int getcwd(char *buf, size_t size)
{
    if (size < strlen(current->fs.cwd) + 1)
        return -1;

    strcpy(buf, current->fs.cwd);
    return 0;
}
SYSCALL_DECL2(getcwd, char*, buf, size_t, size)
{
    return getcwd(buf, size);
}
