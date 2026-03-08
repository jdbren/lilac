// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/process.h>
#include <lilac/libc.h>
#include <lilac/lilac.h>
#include <lilac/clone.h>
#include <lilac/elf.h>
#include <lilac/fs.h>
#include <lilac/sched.h>
#include <lilac/syscall.h>
#include <lilac/timer.h>
#include <lilac/uaccess.h>
#include <lilac/wait.h>
#include <mm/mm.h>
#include <mm/kmm.h>
#include <mm/kmalloc.h>
#include <mm/page.h>

#pragma GCC diagnostic ignored "-Warray-bounds"

#define INIT_STACK(KSTACK) ((uintptr_t)KSTACK + __KERNEL_STACK_SZ - sizeof(size_t))

static atomic_int num_tasks;
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

inline struct task *get_any_pgrp_member(pid_t pgid)
{
    struct task *p;
    if (pgid < 0)
        return NULL;
    hash_for_each_possible(pgid_table, p, pgid_hash, pgid) {
        if (p->pgid == pgid)
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
    return current->tgid;
}

SYSCALL_DECL0(gettid)
{
    return current->pid;
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
        pid = p->pid;
    } else {
        p = get_task_by_pid(pid);
        if (!p || !is_child_of(p)) {
            klog(LOG_DEBUG, "setpgid: no such process %d\n", pid);
            return -ESRCH;
        }
    }

    if (is_session_leader(p))
        return -EPERM;

    target = get_any_pgrp_member(pgid);
    if (pid != pgid && (!target || target->sid != p->sid)) {
        klog(LOG_DEBUG, "setpgid: no such process group %d in session %d\n", pgid, p->sid);
        return -EPERM;
    }

    hash_del(&p->pgid_hash);
    p->pgid = pgid;
    hash_add(pgid_table, &p->pgid_hash, p->pgid);
    klog(LOG_DEBUG, "Set pgid of process %d to %d\n", p->pid, p->pgid);
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
    klog(LOG_DEBUG, "Process %d created new session %d\n", p->pid, p->sid);
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
    return sh;
}

struct fs_info * alloc_fs_info()
{
    struct fs_info *fs = kzmalloc(sizeof(*fs));
    if (!fs) return NULL;
    fs->ref_count = 1;
    return fs;
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
#ifdef DEBUG_FORK
    for (u32 i = 0; i < argc; i++) {
        klog(LOG_DEBUG, "Arg %d: %s\n", i, argv[i]);
    }
#endif

    return argc;
}

static int prepare_task_env(struct task *p, char **envp, char *env_loc, char *env_max)
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
#ifdef DEBUG_FORK
    for (u32 i = 0; i < envc; i++) {
        klog(LOG_DEBUG, "Env %d: %s\n", i, envp[i]);
    }
#endif
    return envc;
}

static unsigned int count_task_vec(char *const vec[])
{
    unsigned int cnt = 0;
    if (!vec)
        return 0;
    while (vec[cnt] && cnt < 31)
        cnt++;
    return cnt;
}

static void start_process(void)
{
    sched_post_switch_unlock();
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
    mem->start_stack = (uintptr_t)(__USER_STACK - __USER_STACK_SZ);
    set_vm_areas(mem);

    unsigned int argc = count_task_vec(current->info.argv);
    unsigned int envc = count_task_vec(current->info.envp);

    // Copy argument and environment strings into brk area
    char *env_loc = (char*)mem->brk;
    char *arg_loc = env_loc + 0x1400;
    sbrk(0x2000);

    /*
     * Set up the initial user stack per the System V ABI convention
     * expected by musl's _start / _start_c / __libc_start_main:
     *
     *   sp[0]              = argc
     *   sp[1 .. argc]      = argv[0 .. argc-1]
     *   sp[argc+1]         = NULL              (argv terminator)
     *   sp[argc+2 .. +1+envc] = envp[0 .. envc-1]
     *   sp[argc+envc+2]    = NULL              (envp terminator)
     *   sp[...]            = 0, 0              (AT_NULL auxiliary vector)
     *
     * _start_c(long *p):  argc = p[0]; argv = (void*)(p+1);
     * __libc_start_main:  envp = argv + argc + 1;
     *                     __auxv scanned past envp NULL terminator.
     */
    size_t nslots = 1 + (argc + 1) + (envc + 1) + 2;
    uintptr_t *sp = (uintptr_t *)__USER_STACK - nslots;
    sp = (uintptr_t *)((uintptr_t)sp & ~(uintptr_t)0xf);

    sp[0] = (uintptr_t)argc;

    char **argv_ptr = (char **)(sp + 1);
    char **envp_ptr = argv_ptr + argc + 1;

    if (!current->info.argv) {
        argv_ptr[0] = NULL;
    } else {
        prepare_task_args(current, argv_ptr, arg_loc, arg_loc + 0xc00);
    }

    if (!current->info.envp) {
        envp_ptr[0] = NULL;
    } else {
        prepare_task_env(current, envp_ptr, env_loc, arg_loc);
    }

    // Empty auxiliary vector (AT_NULL = 0)
    uintptr_t *auxv = (uintptr_t *)(envp_ptr + envc + 1);
    auxv[0] = 0;
    auxv[1] = 0;

    klog(LOG_DEBUG, "Going to user mode, jmp = %p, sp = %p, argc = %d, argv = %p, envp = %p\n",
        jmp, sp, argc, argv_ptr, envp_ptr);
    jump_usermode(jmp, (void*)sp, current->kstack);
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
    this->tgid = this->pid;
    this->tg_leader = this;
    this->exit_signal = SIGCHLD;
    this->mm = mem;
    this->pgd = mem->pgd;
    this->kstack = (void*)INIT_STACK(mem->kstack);
    this->pc = (uintptr_t)(start_process);
    this->state = TASK_RUNNING;
    this->fs = alloc_fs_info();
    this->files = alloc_fdtable(8);
    this->fs->root_d = get_root_dentry();
    this->fs->cwd_d = this->fs->root_d;
    this->info.path = strdup("/sbin/init");
    this->info.argv = kcalloc(2, sizeof(char*));
    this->info.argv[0] = strdup("init");
    this->info.envp = kcalloc(1, sizeof(char*));
    this->sighand = alloc_sighandlers();
    INIT_LIST_HEAD(&this->children);
    INIT_LIST_HEAD(&this->sibling);
    hash_add(pid_table, &this->pid_hash, this->pid);
    hash_add(pgid_table, &this->pgid_hash, this->pgid);
    hash_add(sid_table, &this->sid_hash, this->sid);
    INIT_LIST_HEAD(&this->timer_ev_list);

    return this;
}


static void mm_release(struct task *p, struct mm_info *mm)
{
    if (p->clear_child_tid) {
        if (atomic_load(&mm->ref_count) > 1) {
            put_user(0, p->clear_child_tid);
            // do_futex(p->clear_child_tid, FUTEX_WAKE, 1, NULL, NULL, 0, 0);
        }
        p->clear_child_tid = NULL;
    }

    if (p->vfork_done)
        wake_all(p->vfork_done);
}

void exit_mm_release(struct task *tsk, struct mm_info *mm)
{
    // futex_exit_release(tsk);
    mm_release(tsk, mm);
}

void exec_mm_release(struct task *tsk, struct mm_info *mm)
{
    // futex_exec_release(tsk);
    mm_release(tsk, mm);
}

static void copy_fs_info(struct fs_info *dst, struct fs_info *src)
{
    dget(src->root_d);
    dget(src->cwd_d);
    dst->root_d = src->root_d;
    dst->cwd_d = src->cwd_d;
}

static void copy_files(struct fdtable *dst, struct fdtable *src)
{
    dst->fdarray = kcalloc(src->max, sizeof(struct file*));
    dst->max = src->max;
    for (size_t i = 0; i < src->max; i++) {
        dst->fdarray[i] = src->fdarray[i];
        if (dst->fdarray[i]) {
            fget(dst->fdarray[i]);
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

// TODO error handling
static struct task * clone_process(struct clone_args *args)
{
    unsigned long flags = args->flags;
    struct task *cur = current, *parent = current;
    struct task *child = dup_task(cur);
    if (IS_ERR_OR_NULL(child))
        return child;

    child->rq_node = (struct rb_node){0};
    INIT_LIST_HEAD(&child->children);
    child->on_rq = false;
    child->parent_wait = false;

    child->pid = ++num_tasks;
    if (flags & CLONE_THREAD) {
        child->tgid = cur->tgid;
        child->tg_leader = cur->tg_leader;
    } else {
        child->tgid = child->pid;
        child->tg_leader = child;
    }

    child->set_child_tid = (flags & CLONE_CHILD_SETTID) ? args->child_tid : NULL;
    child->clear_child_tid = (flags & CLONE_CHILD_CLEARTID) ? args->child_tid : NULL;

    if (flags & CLONE_SETTLS) {
        child->tls = args->tls;
    }

    if (flags & (CLONE_PARENT|CLONE_THREAD)) {
        parent = cur->parent;
        child->parent = parent;
        child->ppid = parent->pid;
        if (flags & CLONE_THREAD)
            child->exit_signal = -1;
        else
            child->exit_signal = cur->tg_leader->exit_signal;
    } else {
        child->parent = cur;
        child->ppid = cur->pid;
        child->exit_signal = args->exit_signal;
    }

    list_add_tail(&child->sibling, &parent->children);
    hash_add(pid_table, &child->pid_hash, child->pid);
    hash_add(pgid_table, &child->pgid_hash, parent->pgid);
    hash_add(sid_table, &child->sid_hash, parent->sid);

    if (flags & CLONE_VM) {
        cur->mm->ref_count++;
        child->mm->kstack = kmalloc(__KERNEL_STACK_SZ);
    } else {
        child->mm = arch_copy_mmap(cur->mm);
    }
    child->pgd = child->mm->pgd;
    child->kstack = (void*)INIT_STACK(child->mm->kstack);

    child->regs = arch_copy_regs(cur->regs);
    if (args->stack) {
        klog(LOG_DEBUG, "Setting user stack pointer to %p\n", args->stack);
        arch_set_user_sp(child, args->stack);
    }
    copy_fp_regs(child, cur);

    child->info.path = strdup(cur->info.path);
    child->info.argv = NULL;
    child->info.envp = NULL;
    fget(child->info.exec_file);

    if (flags & CLONE_FS) {
        child->fs->ref_count++;
    } else {
        child->fs = alloc_fs_info();
        copy_fs_info(child->fs, cur->fs);
    }

    if (flags & CLONE_FILES) {
        child->files->ref_count++;
    } else {
        child->files = alloc_fdtable(cur->files->max);
        copy_files(child->files, cur->files);
    }

    if (flags & CLONE_SIGHAND) {
        get_sighandlers(child->sighand);
    } else {
        child->sighand = alloc_sighandlers();
        copy_sighandlers(child->sighand, cur->sighand);
    }
    child->pending = 0;

    INIT_LIST_HEAD(&child->timer_ev_list);

    return child;
}

static void return_from_fork(void)
{
    struct task *p;
    sched_post_switch_unlock();
    p = current;
    if (p->set_child_tid) {
        klog(LOG_DEBUG, "PID %d: Setting child TID at %p\n", p->pid, p->set_child_tid);
        put_user(p->pid, p->set_child_tid);
    }
    klog(LOG_DEBUG, "PID %d: Returning from fork\n", p->pid);
    arch_return_from_fork(p->regs, p->kstack);
}

static void wait_for_vfork_done(struct task *p, struct waitqueue *wq)
{
    klog(LOG_DEBUG, "PID %d: Waiting for vfork child to complete\n", p->pid);
    sleep_on(wq);
    klog(LOG_DEBUG, "PID %d: vfork child completed, resuming\n", p->pid);
}

int do_clone(struct clone_args *args)
{
    struct waitqueue vfork_wait = WAITQUEUE_INIT(vfork_wait);
    unsigned long flags = args->flags;

    klog(LOG_DEBUG, "PID %d: Cloning process with flags 0x%lx\n", current->pid, flags);

    struct task *child = clone_process(args);
    if (!child)
        return -ENOMEM;
    if (IS_ERR(child))
        return PTR_ERR(child);

    if (flags & CLONE_PARENT_SETTID) {
        put_user(child->pid, args->parent_tid);
    }

    child->pc = (uintptr_t)return_from_fork;
    child->state = TASK_RUNNING;
    schedule_task(child);

    if (flags & CLONE_VFORK) {
        child->vfork_done = &vfork_wait;
        wait_for_vfork_done(child, &vfork_wait);
    }

    return child->pid;
}

SYSCALL_DECL0(fork)
{
    struct clone_args args = {
        .exit_signal = SIGCHLD,
    };

    return do_clone(&args);
}

SYSCALL_DECL0(vfork)
{
    struct clone_args args = {
        .flags = CLONE_VFORK|CLONE_VM,
        .exit_signal = SIGCHLD,
    };

    return do_clone(&args);
}

SYSCALL_DECL5(clone, unsigned long, flags, void*, stack, void*, ptid, void*, ctid, unsigned long, tls)
{
    if (flags & CLONE_THREAD) {
        if (!(flags & (CLONE_VM|CLONE_SIGHAND)))
            return -EINVAL;
    }

    struct clone_args args = {
        .flags = flags,
        .stack = stack,
        .parent_tid = ptid,
        .child_tid = ctid,
        .tls = (void*)tls,
        .exit_signal = flags & 0xff ? flags & 0xff : SIGCHLD,
    };

    int pid = do_clone(&args);

    return pid;
}

__noreturn
static void exec_and_return(void)
{
    klog(LOG_DEBUG, "Entering exec_and_return, pid = %d\n", current->pid);
    struct mm_info *old_mm = current->mm;
    struct mm_info *mem = arch_process_remap(current->mm);
    struct task *task = current;

    task->mm = mem;
    task->pgd = mem->pgd;
    task->kstack = (void*)INIT_STACK(mem->kstack);
    task->pc = (uintptr_t)start_process;
    task->state = TASK_RUNNING;
    // Reset custom signal handlers to default
    for (int i = 0; i < _NSIG; i++) {
        if ((long)task->sighand->actions[i].sa.sa_handler > (long)SIG_IGN) {
            task->sighand->actions[i].sa.sa_handler = SIG_DFL;
        }
    }

    exec_mm_release(current, old_mm);

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
    klog(LOG_DEBUG, "execve called with path = %s, argv = %p, envp = %p\n", path, argv, envp);
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

static void cleanup_fs(struct fs_info *fs, struct fdtable *files)
{
    if (!--files->ref_count) {
        for (size_t i = 0; i < files->max; i++) {
            if (files->fdarray[i]) {
                struct file *file = files->fdarray[i];
    #ifdef DEBUG_VFS
                klog(LOG_DEBUG, "Cleaning up file descriptor %d, file %p\n", i, file);
    #endif
                vfs_close(file);
                files->fdarray[i] = NULL;
            }
        }
        kfree(files->fdarray);
        kfree(files);
    }

    if (!--fs->ref_count) {
        dput(fs->cwd_d);
        kfree(fs);
    }
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
    if (!--mm->ref_count)
        arch_unmap_all_user_vm(mm);
    exit_mm_release(current, mm);
    // mm_struct is freed in reap_task() since we need original kstack
}

void cleanup_task(struct task *p)
{
    klog(LOG_DEBUG, "Cleaning up task %d\n", p->pid);
    cleanup_task_info(&p->info);
    cleanup_fs(p->fs, p->files);
    cleanup_memory(p->mm);
    put_sighandlers(p->sighand);
}

void reap_task(struct task *p)
{
    if (p->state != TASK_ZOMBIE) {
        klog(LOG_WARN, "Tried to reap task %d, but it is not dead\n", p->pid);
        return;
    }
    klog(LOG_DEBUG, "Reaping task %d\n", p->pid);
    kfree(p->fp_regs);
    p->fp_regs = NULL;
    list_del(&p->sibling);
    hash_del(&p->pid_hash);
    hash_del(&p->pgid_hash);
    hash_del(&p->sid_hash);
    // kfree(p->regs); // ISSUE This is sometimes stack memory, so can't free currently
    free_pages(p->mm->kstack, __KERNEL_STACK_SZ / PAGE_SIZE);
    if (p->mm->ref_count == 0) {
        arch_reclaim_mem(p);
        kfree(p->mm);
    }
}

__noreturn void do_exit(void)
{
    struct task *parent = NULL;
    if (unlikely(current->pid == 1))
        panic("Init tried to exit!\n");
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

    acquire_lock(&task->fs->lock);
    dput(task->fs->cwd_d);
    dget(d);
    task->fs->cwd_d = d;
    release_lock(&task->fs->lock);
out:
    kfree(path_buf);
    return err;
}
