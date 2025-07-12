// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/process.h>
#include <stdatomic.h>
#include <lilac/rbtree.h>
#include <lilac/libc.h>
#include <lilac/lilac.h>
#include <lilac/elf.h>
#include <lilac/fs.h>
#include <lilac/sched.h>
#include <lilac/wait.h>
#include <lilac/timer.h>
#include <lilac/syscall.h>
#include <lilac/mm.h>
#include <mm/kmm.h>
#include <mm/kmalloc.h>

#define INIT_STACK(KSTACK) ((uintptr_t)KSTACK + __KERNEL_STACK_SZ - sizeof(size_t))

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

static void * load_executable(struct task *p)
{
    const char *path = p->info.path;
    struct file *file = p->info.exec_file;
    void *jmp = NULL;

    if (!file) {
        file = vfs_open(path, 0, 0);
        if (IS_ERR_OR_NULL(file)) {
            klog(LOG_ERROR, "Failed to open file %s\n", path);
            exit(1);
        }
        p->info.exec_file = file;
    }

    int bytes = file->f_dentry->d_inode->i_size;
    struct elf_header *hdr = kzmalloc(bytes);
    klog(LOG_DEBUG, "Reading ELF file\n");
    klog(LOG_DEBUG, "File size: %d\n", bytes);
    if (vfs_read(file, (void*)hdr, bytes) != bytes) {
        klog(LOG_ERROR, "Failed to read file %s\n", path);
        goto out;
    }

    klog(LOG_DEBUG, "Parsing ELF file\n");
    jmp = elf_load(hdr, p->mm);
    if (!jmp) {
        klog(LOG_ERROR, "Failed to load ELF file\n");
        goto out;
    }

out:
    //kfree(hdr);
    klog(LOG_DEBUG, "ELF loaded\n");
    return jmp;
}

static void set_vm_areas(struct mm_info *mem)
{
    struct vm_desc *stack_desc = kzmalloc(sizeof(*stack_desc));
    stack_desc->mm = mem;
    stack_desc->start = mem->start_stack;
    stack_desc->end = __USER_STACK;
    stack_desc->vm_prot = PROT_READ | PROT_WRITE;
    stack_desc->vm_flags = MAP_PRIVATE | MAP_ANONYMOUS;
    vma_list_insert(stack_desc, &mem->mmap);
#ifdef DEBUG_VMA
    struct vm_desc *desc = mem->mmap;
    while (desc) {
        klog(LOG_DEBUG, "Start: %x\n", desc->start);
        klog(LOG_DEBUG, "End: %x\n", desc->end);
        klog(LOG_DEBUG, "Prot: %x\n", desc->vm_prot);
        klog(LOG_DEBUG, "Flags: %x\n", desc->vm_flags);
        desc = desc->vm_next;
    }
#endif
}

static void * prepare_task_args(struct task *p, uintptr_t *stack)
{
    // Write args to user stack
    u32 argc = 0;
    char **argv = (char**)(stack);
    for (; p->info.argv[argc] && argc < 31; argc++) {
        u32 len = strlen(p->info.argv[argc]) + 1;
        len = (len + sizeof(void*)-1) & ~(sizeof(void*)-1); // Align stack
        stack = (void*)((uintptr_t)stack - len);
        strcpy((char*)stack, p->info.argv[argc]);
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
    return stack;
}

static void start_process(void)
{
    struct mm_info *mem = current->mm;
    klog(LOG_DEBUG, "Process %d starting\n", current->pid);

    void *jmp = load_executable(current);

    struct vm_desc *desc = mem->mmap;
    while (desc) { // after the data segment, this seems imprecise?
        if ((desc->vm_prot & (PROT_READ|PROT_WRITE)) == (PROT_READ|PROT_WRITE))
            break;
        desc = desc->vm_next;
    }
    mem->brk = desc ? desc->end : 0;
    mem->start_stack = (uintptr_t)arch_user_stack();
    set_vm_areas(mem);

    uintptr_t *stack = (void*)(__USER_STACK - 128); // Will place argv entries here
    if (!current->info.argv) {
        *stack = 0;
        *--stack = 0;
        goto skip;
    } else {
        stack = prepare_task_args(current, stack);
    }

skip:
    klog(LOG_DEBUG, "Going to user mode, jmp = %x\n", jmp);
    jump_usermode(jmp, (void*)stack, current->kstack);
}

struct task *init_process(void)
{
    num_tasks = 1;
    struct task *this = kzmalloc(sizeof(*this));
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
    this->fs.root_d = get_root_dentry();
    this->fs.cwd_d = this->fs.root_d;
    this->info.path = "/sbin/init";
    memcpy(this->name, "init", 5);

    INIT_LIST_HEAD(&this->children);

    return this;
}

static void copy_fs_info(struct fs_info *dst, struct fs_info *src)
{
    dget(src->root_d);
    dget(src->cwd_d);
    dst->root_d = src->root_d;
    dst->cwd_d = src->cwd_d;
    dst->files.fdarray = kcalloc(src->files.max, sizeof(struct file*));
    dst->files.max = src->files.max;
    for (size_t i = 0; i < src->files.max; i++) {
        dst->files.fdarray[i] = src->files.fdarray[i];
        if (dst->files.fdarray[i]) {
            fget(dst->files.fdarray[i]);
        }
    }
}

static struct task *dup_task(struct task *p)
{
    struct task *new_task = kzmalloc(sizeof(*new_task));
    if (!new_task)
        return NULL;
    *new_task = *p;
    return new_task;
}

static struct task * clone_process(struct task *parent)
{
    struct task *child = dup_task(parent);
    if (!child)
        return NULL;
    child->rq_node = (struct rb_node){0};
    INIT_LIST_HEAD(&child->children);
    child->on_rq = false;
    child->parent_wait = false;

    child->pid = ++num_tasks;
    child->parent = parent;
    child->ppid = parent->pid;
    list_add_tail(&child->sibling, &parent->children);

    child->mm = arch_copy_mmap(parent->mm);
    child->pgd = child->mm->pgd;
    child->kstack = (void*)INIT_STACK(child->mm->kstack);

    child->regs = arch_copy_regs(parent->regs);
    copy_fp_regs(child, parent);

    child->info.path = strdup(parent->info.path);
    child->info.argv = NULL;
    fget(child->info.exec_file);
    copy_fs_info(&child->fs, &parent->fs);

    strncat(child->name, " (fork)", 31);
    return child;
}

static void return_from_fork(void)
{
    klog(LOG_DEBUG, "PID %d: Returning from fork\n", current->pid);
    arch_return_from_fork(current->regs, current->kstack);
}

int do_fork(void)
{
    struct task *parent = current;
    struct task *child = clone_process(parent);
    if (!child)
        return -ENOMEM;

    child->pc = (uintptr_t)return_from_fork;
    child->state = TASK_RUNNING;
    schedule_task(child);

    return child->pid;
}

__noreturn
static void exec_and_return(void)
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
        ssize_t len = strnlen_user(argv[i], 127) + 1;
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

static void cleanup_files(struct fs_info *fs)
{
    dput(fs->cwd_d);
    for (size_t i = 0; i < fs->files.max; i++) {
        if (fs->files.fdarray[i]) {
            struct file *file = fs->files.fdarray[i];
#ifdef DEBUG_VFS
            klog(LOG_DEBUG, "Cleaning up file descriptor %d, file %p\n", i, file);
#endif
            vfs_close(file);
            fs->files.fdarray[i] = NULL;
        }
    }
    kfree(fs->files.fdarray);
}

static void cleanup_task_info(struct task_info *info)
{
    klog(LOG_DEBUG, "Cleaning up task info\n");
    if (info->path) {
        kfree((void*)info->path);
        info->path = NULL;
    }
    if (info->exec_file) {
        vfs_close(info->exec_file);
        info->exec_file = NULL;
    }
    if (info->argv) {
        for (int i = 0; info->argv[i]; i++)
            kfree(info->argv[i]);
        kfree(info->argv);
        info->argv = NULL;
    }
}

static void cleanup_memory(struct mm_info *mm)
{
    klog(LOG_DEBUG, "Cleaning up memory\n");
    arch_unmap_all_user_vm(mm);
    // mm_struct is freed in reap_task() since we need original kstack
}

void cleanup_task(struct task *p)
{
    klog(LOG_DEBUG, "Cleaning up task %d\n", p->pid);
    cleanup_task_info(&p->info);
    cleanup_files(&p->fs);
    cleanup_memory(p->mm);
}

void reap_task(struct task *p)
{
    if (p->state != TASK_ZOMBIE) {
        klog(LOG_WARN, "Tried to reap task %d, but it is not dead\n", p->pid);
        return;
    }
    klog(LOG_DEBUG, "Reaping task %d\n", p->pid);
    arch_reclaim_mem(p);
    kfree(p->fp_regs);
    p->fp_regs = NULL;
    list_del(&p->sibling);
    // kfree(p->regs); // ISSUE This is sometimes stack memory, so can't free currently
    kvirtual_free(p->mm->kstack, __KERNEL_STACK_SZ);
    kfree(p->mm);
}

__noreturn void exit(int status)
{
    struct task *parent = NULL;
    set_task_sleeping(current);
    klog(LOG_INFO, "Process %d exited with status %d\n", current->pid, status);
    if (current->parent_wait || current->parent->waiting_any)
        parent = current->parent;
    cleanup_task(current);
    current->exit_val = status;
    current->state = TASK_ZOMBIE;
    if (parent)
        notify_parent(parent, current);
    schedule();
    kerror("exit: Should never be reached\n");
    unreachable();
}
SYSCALL_DECL1(exit, int, status)
{
    exit(status);
    return -1;
}


SYSCALL_DECL1(chdir, const char*, path)
{
    char *path_buf = kmalloc(256);
    long err = 0;
    struct task *task = current;

    err = strncpy_from_user(path_buf, path, 255);
    if (err < 0) {
        kfree(path_buf);
        return err;
    }

    struct dentry *d = vfs_lookup(path_buf);
    if (IS_ERR(d)) {
        err = PTR_ERR(d);
        goto out;
    } else if (!d || !d->d_inode) {
        err = -ENOENT;
        goto out;
    }

    if (d->d_inode->i_type != TYPE_DIR) {
        err = -ENOTDIR;
        goto out;
    }

    arch_disable_interrupts();
    dput(task->fs.cwd_d);
    dget(d);
    task->fs.cwd_d = d;
    // arch_enable_interrupts();
out:
    kfree(path_buf);
    return err;
}
