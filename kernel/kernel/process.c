// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/process.h>
#include <stdatomic.h>
#include <lib/rbtree.h>
#include <lib/hashtable.h>
#include <lilac/libc.h>
#include <lilac/lilac.h>
#include <lilac/elf.h>
#include <lilac/fs.h>
#include <lilac/sched.h>
#include <lilac/wait.h>
#include <lilac/timer.h>
#include <lilac/syscall.h>
#include <mm/mm.h>
#include <mm/kmm.h>
#include <mm/kmalloc.h>
#include <mm/page.h>
#include <lilac/uaccess.h>

#define INIT_STACK(KSTACK) ((uintptr_t)KSTACK + __KERNEL_STACK_SZ - sizeof(size_t))

static int num_tasks;
DEFINE_HASHTABLE(pid_table, PID_HASH_BITS);
DEFINE_HASHTABLE(pgid_table, PID_HASH_BITS);
DEFINE_HASHTABLE(sid_table, PID_HASH_BITS);

void exit(int status);

static inline bool is_session_leader(struct task *p)
{
    return p->pid == p->sid;
}

static inline bool is_child_of(struct task *cld)
{
    struct task *p_itr;
    list_for_each_entry(p_itr, &current->children, sibling) {
        if (cld == p_itr) return true;
    }
    return false;
}

inline struct task * get_task_by_pid(int pid)
{
    struct task *p;
    if (pid < 1) return NULL;
    hash_for_each_possible(pid_table, p, pid_hash, pid) {
        if (p->pid == pid)
            return p;
    }
    return NULL;
}

inline struct task * get_pgrp_leader(int pgid)
{
    struct task *p;
    if (pgid < 0) return NULL;
    hash_for_each_possible(pgid_table, p, pgid_hash, pgid) {
        if (p->pgid == pgid && p->pid == pgid)
            return p;
    }
    return NULL;
}


inline int get_pid(void)
{
    return current->pid;
}

SYSCALL_DECL0(getpid)
{
    return get_pid();
}

SYSCALL_DECL0(getppid)
{
    return current->ppid;
}

SYSCALL_DECL1(getpgid, pid_t, pid)
{
    if (pid == 0)
        return current->pgid;
    struct task *p = get_task_by_pid(pid);
    return p ? p->pgid : -ESRCH;
}

SYSCALL_DECL1(getsid, pid_t, pid)
{
    if (pid == 0)
        return current->sid;
    struct task *p = get_task_by_pid(pid);
    return p ? p->sid : -ESRCH;
}

SYSCALL_DECL2(setpgid, pid_t, pid, pid_t, pgid)
{
    struct task *p, *target;
    if (pgid < 0) return -EINVAL;

    if (pid == 0 || pid == current->pid) {
        p = current;
    } else {
        p = get_task_by_pid(pid);
        if (!p || !is_child_of(p))
            return -ESRCH;
    }

    if (is_session_leader(p))
        return -EPERM;

    target = get_pgrp_leader(pgid);
    if (pid != pgid && (!target || target->sid != p->sid))
        return -EPERM;

    hash_del(&p->pgid_hash);
    p->pgid = pgid;
    hash_add(pgid_table, &p->pgid_hash, p->pgid);
    return 0;
}

SYSCALL_DECL0(setsid)
{
    struct task *p = current;
    int pid = p->pid;
    if (p->pgid == pid)
        return -EPERM;

    hash_del(&p->sid_hash);
    hash_del(&p->pgid_hash);
    p->sid  = pid;
    p->pgid = pid;
    p->ctty = NULL;
    hash_add(sid_table, &p->sid_hash, p->sid);
    hash_add(pgid_table, &p->pgid_hash, p->pgid);
    return pid;
}

static inline void get_sighandlers(struct sighandlers *sh)
{
    sh->ref_count++;
}

static inline void put_sighandlers(struct sighandlers *sh)
{
    if (--sh->ref_count == 0)
        kfree(sh);
}

struct sighandlers * alloc_sighandlers(void)
{
    struct sighandlers *sh = kzmalloc(sizeof(*sh));
    if (!sh)
        return NULL;
    sh->ref_count = 1;
    sh->lock = (spinlock_t)SPINLOCK_INIT;
    return sh;
}

static void * load_executable(struct task *p)
{
    const char *path = p->info.path;
    struct file *file = p->info.exec_file;
    void *jmp = NULL;

    if (!file) {
        file = vfs_open(path, 0, 0);
        if (IS_ERR_OR_NULL(file)) {
            klog(LOG_ERROR, "Failed to open file %s: %ld\n", path, PTR_ERR(file));
            exit(1);
        }
        p->info.exec_file = file;
    }

    jmp = elf_load(file, p->mm);
    if (!jmp) {
        klog(LOG_ERROR, "Failed to load ELF file\n");
        goto out;
    }

out:
    klog(LOG_DEBUG, "ELF loaded\n");
    return jmp;
}

#define DEBUG_VMA

static void set_vm_areas(struct mm_info *mem)
{
    struct vm_desc *stack_desc = kzmalloc(sizeof(*stack_desc));
    stack_desc->mm = mem;
    stack_desc->start = mem->start_stack;
    stack_desc->end = __USER_STACK;
    stack_desc->vm_flags = VM_READ | VM_WRITE;
    vma_list_insert(stack_desc, &mem->mmap);
#ifdef DEBUG_VMA
    struct vm_desc *desc = mem->mmap;
    klog(LOG_DEBUG, "VM Areas:\n");
    while (desc) {
        klog(LOG_DEBUG, "-----\n");
        klog(LOG_DEBUG, "Start: %x\n", desc->start);
        klog(LOG_DEBUG, "End: %x\n", desc->end);
        klog(LOG_DEBUG, "Flags: %x\n", desc->vm_flags);
        desc = desc->vm_next;
    }
#endif
}

static unsigned int prepare_task_args(struct task *p, char **argv, char *argv_loc, char *argv_max)
{
    // Write args to user stack
    unsigned int argc = 0;
    p->mm->arg_start = (uintptr_t)argv_loc;
    for (; p->info.argv[argc] && argc < 31; argc++) {
        u32 len = strlen(p->info.argv[argc]) + 1;
        strcpy(argv_loc, p->info.argv[argc]);
        argv[argc] = argv_loc;
        argv_loc += len;
        assert(argv_loc < argv_max);
    }
    p->mm->arg_end = (uintptr_t)argv_loc;
    argv[argc] = NULL;

    // Print the arguments
    for (u32 i = 0; i < argc; i++) {
        klog(LOG_DEBUG, "Arg %d: %s\n", i, argv[i]);
    }

    return argc;
}

static void prepare_task_env(struct task *p, char **envp, char *env_loc, char *env_max)
{
    // Write env to user stack
    u32 envc = 0;
    p->mm->env_start = (uintptr_t)env_loc;
    for (; p->info.envp[envc] && envc < 31; envc++) {
        u32 len = strlen(p->info.envp[envc]) + 1;
        strcpy(env_loc, p->info.envp[envc]);
        envp[envc] = env_loc;
        env_loc = env_loc + len;
        assert(env_loc < env_max);
    }
    p->mm->env_end = (uintptr_t)env_loc;
    envp[envc] = NULL;

    // Print the environment variables
    for (u32 i = 0; i < envc; i++) {
        klog(LOG_DEBUG, "Env %d: %s\n", i, envp[i]);
    }
}

static void start_process(void)
{
    struct mm_info *mem = current->mm;
    klog(LOG_DEBUG, "Process %d starting\n", current->pid);

    void *jmp = load_executable(current);
    if (!jmp) {
        klog(LOG_ERROR, "Failed to load executable, exiting\n");
        exit(1);
    }

    struct vm_desc *desc = mem->mmap;
    while (desc) { // after the data segment, this seems imprecise?
        if ((desc->vm_flags & (VM_READ|VM_WRITE)) == (VM_READ|VM_WRITE))
            break;
        desc = desc->vm_next;
    }
    mem->start_brk = mem->brk = desc ? desc->end : 0;
    mem->start_stack = (uintptr_t)arch_user_stack();
    set_vm_areas(mem);

    uintptr_t argv, argc, envp;
    char *env_loc = (char*)mem->brk;
    char *arg_loc = env_loc + 0x1400;
    sbrk(0x2000);

    uintptr_t *stack = (void*)(__USER_STACK - sizeof(void*)*16);
    if (!current->info.envp) {
        envp = 0;
    } else {
        envp = (uintptr_t)stack;
        prepare_task_env(current, (char**)stack, env_loc, arg_loc);
    }

    stack = (void*)((uintptr_t)stack - sizeof(void*)*32);
    if (!current->info.argv) {
        argv = 0;
        argc = 0;
    } else {
        argv = (uintptr_t)stack;
        argc = prepare_task_args(current, (char**)stack, arg_loc, arg_loc + 0xc00);
    }

    stack = (void*)((uintptr_t)stack & ~(sizeof(void*)*2 - 1));
    --stack;
    *--stack = envp;
    *--stack = argv;
    *--stack = argc;

    klog(LOG_DEBUG, "Going to user mode, jmp = %p, stack = %p\n", jmp, stack);
    jump_usermode(jmp, (void*)stack, current->kstack);
}

struct task *init_process(void)
{
    num_tasks = 1;
    struct task *this = kzmalloc(sizeof(*this));
    struct mm_info *mem = arch_process_mmap(sizeof(void*) == 8);

    this->pid = 1;
    this->ppid = 0;
    this->pgid = this->pid;
    this->sid = this->pid;
    this->mm = mem;
    this->pgd = mem->pgd;
    this->kstack = (void*)INIT_STACK(mem->kstack);
    this->pc = (uintptr_t)(start_process);
    this->state = TASK_RUNNING;
    this->fs.files.fdarray = kcalloc(4, sizeof(struct file));
    this->fs.files.max = 4;
    this->fs.root_d = get_root_dentry();
    this->fs.cwd_d = this->fs.root_d;
    this->info.path = strdup("/sbin/init");
    this->info.argv = kcalloc(2, sizeof(char*));
    this->info.argv[0] = strdup("init");
    this->info.envp = kcalloc(1, sizeof(char*));
    memcpy(this->name, "init", 5);
    this->sighandlers = alloc_sighandlers();
    INIT_LIST_HEAD(&this->children);
    hash_add(pid_table, &this->pid_hash, this->pid);
    hash_add(pgid_table, &this->pgid_hash, this->pgid);
    hash_add(sid_table, &this->sid_hash, this->sid);

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

static void copy_sighandlers(struct sighandlers *dst, struct sighandlers *src)
{
    for (int i = 0; i < _NSIG; i++)
        dst->actions[i] = src->actions[i];
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
    hash_add(pid_table, &child->pid_hash, child->pid);
    hash_add(pgid_table, &child->pgid_hash, parent->pgid);
    hash_add(sid_table, &child->sid_hash, parent->sid);

    child->mm = arch_copy_mmap(parent->mm);
    child->pgd = child->mm->pgd;
    child->kstack = (void*)INIT_STACK(child->mm->kstack);

    child->regs = arch_copy_regs(parent->regs);
    copy_fp_regs(child, parent);

    child->info.path = strdup(parent->info.path);
    child->info.argv = NULL;
    child->info.envp = NULL;
    fget(child->info.exec_file);
    copy_fs_info(&child->fs, &parent->fs);

    child->sighandlers = alloc_sighandlers();
    copy_sighandlers(child->sighandlers, parent->sighandlers);

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

SYSCALL_DECL0(fork)
{
    return do_fork();
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
    panic("exec_and_return: Should never be reached\n");
    unreachable();
}

static int set_task_args(struct task_info *info, char *const argv[])
{
    int i, err = 0;
    if (info->argv) {
        for (i = 0; info->argv[i]; i++)
            kfree(info->argv[i]);
        kfree(info->argv);
        info->argv = NULL;
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

static int set_task_env(struct task_info *info, char *const envp[])
{
    int i, err = 0;
    if (info->envp) {
        for (i = 0; info->envp[i]; i++)
            kfree(info->envp[i]);
        kfree(info->envp);
        info->envp = NULL;
    }
    for (i = 0; envp[i]; i++) {
        ssize_t len = strnlen_user(envp[i], 127) + 1;
        if (len < 0)
            return -EFAULT;
        info->envp = krealloc(info->envp, (i + 1) * sizeof(char*));
        info->envp[i] = kmalloc(len + 1);
        err = strncpy_from_user(info->envp[i], envp[i], len + 1);
        if (err < 0)
            return err;
    }
    info->envp = krealloc(info->envp, (i + 1) * sizeof(char*));
    info->envp[i] = 0;
    return 0;
}

static int do_execve(const char *path, char *const argv[], char *const envp[])
{
    struct task_info *info = &current->info;
    struct file *file = vfs_open(path, 0, 0);

    if (IS_ERR_OR_NULL(file)) {
        klog(LOG_DEBUG, "file %s not found\n", path);
        return PTR_ERR(file);
    } else if (!S_ISREG(file->f_dentry->d_inode->i_mode)) {
        klog(LOG_DEBUG, "file %s is not a regular file\n", path);
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

    err = set_task_env(info, envp);
    if (err) {
        klog(LOG_ERROR, "Failed to set task environment\n");
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
    if (!access_ok(path, 1) || !access_ok(argv, 1) || !access_ok(envp, 1))
        return -EFAULT;

    char *path_buf = get_user_path(path);
    if (IS_ERR(path_buf))
        return PTR_ERR(path_buf);

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
    put_sighandlers(p->sighandlers);
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
    hash_del(&p->pid_hash);
    hash_del(&p->pgid_hash);
    hash_del(&p->sid_hash);
    // kfree(p->regs); // ISSUE This is sometimes stack memory, so can't free currently
    free_pages(p->mm->kstack, __KERNEL_STACK_SZ / PAGE_SIZE);
    kfree(p->mm);
}

__noreturn void do_exit(void)
{
    struct task *parent = NULL;
    if (unlikely(current->pid == 1))
        panic("Init tried to exit!\n");
    if (current->parent_wait || current->parent->waiting_any)
        parent = current->parent;
    cleanup_task(current);
    current->state = TASK_ZOMBIE;
    if (parent)
        notify_parent(parent, current);
    schedule();
    unreachable();
}

__noreturn void exit(int status)
{
    current->exit_status = WEXITED(status);
    klog(LOG_INFO, "Process %d exited with status %d\n", current->pid, status);
    do_exit();
    panic("exit: Should never be reached\n");
    unreachable();
}
SYSCALL_DECL1(exit, int, status)
{
    exit(status);
    return -1;
}


SYSCALL_DECL1(chdir, const char*, path)
{
    char *path_buf;
    long err = 0;
    struct task *task = current;

    path_buf = get_user_path(path);
    if (IS_ERR(path_buf))
        return PTR_ERR(path_buf);

    struct dentry *d = vfs_lookup(path_buf);
    if (IS_ERR(d)) {
        err = PTR_ERR(d);
        goto out;
    } else if (!d || !d->d_inode) {
        err = -ENOENT;
        goto out;
    }

    if (!S_ISDIR(d->d_inode->i_mode)) {
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
