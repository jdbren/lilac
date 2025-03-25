// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/process.h>

#include <stdatomic.h>
#include <string.h>

#include <lilac/lilac.h>
#include <lilac/elf.h>
#include <lilac/fs.h>
#include <lilac/sched.h>
#include <lilac/timer.h>
#include <lilac/syscall.h>
#include <lilac/mm.h>
#include <mm/kmm.h>
#include <mm/kmalloc.h>

#define MAX_TASKS 1024
#define INIT_STACK(KSTACK) ((uintptr_t)KSTACK + __KERNEL_STACK_SZ - sizeof(size_t))

static struct task tasks[MAX_TASKS];
static int num_tasks;

void exit(int status);

inline u32 get_pid(void)
{
    return current->pid;
}

SYSCALL_DECL0(getpid)
{
    return get_pid();
}

static void start_process(void)
{
    struct mm_info *mem = current->mm;
    klog(LOG_DEBUG, "Process %d starting\n", current->pid);

    const char *path = current->info.path;
    struct file *file = current->info.exec_file;
    if (!file) {
        file = vfs_open(path, 0, 0);
        if (IS_ERR_OR_NULL(file)) {
            klog(LOG_ERROR, "Failed to open file %s\n", path);
            exit(1);
        }
    }

    int bytes = file->f_dentry->d_inode->i_size;
    struct elf_header *hdr = kzmalloc(bytes);
    klog(LOG_DEBUG, "Reading ELF file\n");
    klog(LOG_DEBUG, "File size: %d\n", bytes);
    if (vfs_read(file, (void*)hdr, bytes) != bytes) {
        klog(LOG_ERROR, "Failed to read file %s\n", path);
        vfs_close(file);
        kfree(hdr);
        exit(1);
    }

    klog(LOG_DEBUG, "Parsing ELF file\n");
    void *jmp = elf_load(hdr, mem);
    if (!jmp) {
        klog(LOG_ERROR, "Failed to load ELF file\n");
        vfs_close(file);
        kfree(hdr);
        exit(1);
    }

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
    mem->start_brk = __USER_BRK;
    mem->brk = __USER_BRK;

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
        len = (len + sizeof(void*)-1) & ~(sizeof(void*)-1); // Align stack
        stack = (void*)((uintptr_t)stack - len);
        strcpy((char*)stack, current->info.argv[argc]);
        argv[argc] = (char*)stack;
    }
    argv[argc] = NULL;

    // Print the arguments
    for (u32 i = 0; i < argc; i++) {
        klog(LOG_DEBUG, "Arg %d: %s\n", i, argv[i]);
    }

    assert(is_aligned(stack, sizeof(void*)));
    *--stack = (uintptr_t)argv;
    *--stack = argc;

skip:
    klog(LOG_DEBUG, "Going to user mode, jmp = %x\n", jmp);
    jump_usermode(jmp, (void*)stack, current->kstack);
}

struct task *init_process(void)
{
    num_tasks = 1;
    struct task *this = &tasks[1];
    struct mm_info *mem = arch_process_mmap(sizeof(void*) == 8);

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
    this->fs.root = kzmalloc(2);
    this->fs.root[0] = '/';
    this->fs.root[1] = '\0';
    this->info.path = "/sbin/init";
    memcpy(this->name, "init", 5);

    return this;
}

static void copy_fs_info(struct fs_info *dst, struct fs_info *src)
{
    dst->root = strdup(src->root);
    dst->cwd = strdup(src->cwd);
    dst->files.fdarray = kcalloc(src->files.max, sizeof(struct file));
    dst->files.max = src->files.max;
    dst->files.size = src->files.size;
    for (size_t i = 0; i < src->files.size; i++) {
        dst->files.fdarray[i] = src->files.fdarray[i];
        if (dst->files.fdarray[i])
            dst->files.fdarray[i]->f_count++;
    }
}

void clone_process(struct task *parent, struct task *child)
{
    *child = *parent;
    child->ppid = parent->pid;
    child->parent = parent;

    child->mm = arch_copy_mmap(parent->mm);
    child->pgd = child->mm->pgd;
    child->kstack = (void*)INIT_STACK(child->mm->kstack);

    child->info.path = strdup(parent->info.path);
    copy_fs_info(&child->fs, &parent->fs);

    strncat(child->name, " (clone)", 31);
}

static void return_from_fork(void)
{
    klog(LOG_DEBUG, "Returning from fork\n");
    arch_return_from_fork(current->regs, current->kstack);
}

int do_fork(void)
{
    u32 pid = ++num_tasks;
    struct task *parent = current;
    struct task *child = &tasks[pid];

    clone_process(parent, child);
    child->pid = pid;
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
    kerror("exec_and_return: Should never be reached\n");
    unreachable();
}

static int set_task_args(struct task_info *info, char *const argv[])
{
    int i, err = 0;
    if (info->argv) {
        for (i = 0; info->argv[i]; i++)
            kfree(info->argv[i]);
        kfree(info->argv);
    }
    for (i = 0; argv[i]; i++) {
        size_t len = strnlen_user(argv[i], 127) + 1;
        if (len < 0)
            return -EFAULT;
        info->argv = krealloc(info->argv, (i + 1) * sizeof(char*));
        info->argv[i] = kmalloc(len + 1);
        err = strncpy_from_user(info->argv[i], argv[i], len + 1);
        if (err < 0)
            return err;
    }
    info->argv = krealloc(info->argv, (i + 1) * sizeof(char*));
    info->argv[i] = 0;
    return 0;
}

static int do_execve(const char *path, char *const argv[], char *const envp[])
{
    struct task_info *info = &current->info;
    struct file *file = vfs_open(path, 0, 0);

    if (IS_ERR_OR_NULL(file)) {
        klog(LOG_ERROR, "file %s not found\n", path);
        return PTR_ERR(file);
    } else if (file->f_dentry->d_inode->i_type != TYPE_FILE) {
        klog(LOG_ERROR, "file %s is not a regular file\n", path);
        vfs_close(file);
        return -EACCES;
    } else {
        info->exec_file = file;
    }

    if (info->path)
        kfree((void*)info->path);
    info->path = path;

    int err = set_task_args(info, argv);
    if (err) {
        klog(LOG_ERROR, "Failed to set task arguments\n");
        vfs_close(file);
        return err;
    }

    klog(LOG_INFO, "Executing %s\n", info->path);
    exec_and_return();
    unreachable();
}

SYSCALL_DECL3(execve, const char*, path, char* const*, argv, char* const*, envp)
{
    int err = 0;
    char *path_buf = kmalloc(256);
    if (!path_buf)
        return -ENOMEM;

    err = strncpy_from_user(path_buf, path, 255);
    if (err < 0) {
        kfree(path_buf);
        return err;
    }

    err = do_execve(path_buf, argv, envp);
    if (err < 0)
        kfree(path_buf);
    return err;
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

__noreturn void exit(int status)
{
    current->state = TASK_DEAD;
    klog(LOG_INFO, "Process %d exited with status %d\n", current->pid, status);
    schedule();
    kerror("exit: Should never be reached\n");
    unreachable();
}
SYSCALL_DECL1(exit, int, status)
{
    exit(status);
    return -1;
}

int getcwd(char *buf, size_t size)
{
    if (size < strlen(current->fs.cwd) + 1)
        return -1;

    return copy_to_user(buf, current->fs.cwd, size);
}
SYSCALL_DECL2(getcwd, char*, buf, size_t, size)
{
    return getcwd(buf, size);
}
