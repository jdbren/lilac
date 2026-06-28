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

SYSCALL_DECL1(set_tid_address, int *, tidptr)
{
    if (tidptr && !access_ok(tidptr, sizeof(int)))
        return -EFAULT;
    current->clear_child_tid = tidptr;
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


static int load_executable(struct task *p, struct exec_info *info)
{
    const char *path = p->info.path;
    struct file *file = p->info.exec_file;

    if (!file) {
        file = vfs_open(path, 0, 0);
        if (IS_ERR_OR_NULL(file)) {
            klog(LOG_ERROR, "Failed to open file %s: %ld\n", path, PTR_ERR(file));
            exit(1);
        }
        p->info.exec_file = file;
    }

    mmap_write_lock(p->mm);
    int ret = elf_load(file, p->mm, info);
    mmap_write_unlock(p->mm);
    if (ret < 0) {
        klog(LOG_ERROR, "Failed to load ELF file: %d\n", ret);
        return ret;
    }

    klog(LOG_DEBUG, "ELF loaded\n");
    return 0;
}

// Must be called with mm->mmap_lock held
static void set_vm_areas(struct mm_info *mem)
{
    struct vm_desc *stack_desc = kzmalloc(sizeof(*stack_desc));
    stack_desc->mm = mem;
    stack_desc->start = mem->start_stack;
    stack_desc->end = __USER_STACK;
    // TODO set this correctly
    stack_desc->vm_flags = VM_READ | VM_WRITE | VM_EXEC;
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
        // strcpy(argv_loc, p->info.argv[argc]);
        strncpy_to_user(argv_loc, p->info.argv[argc], len);
        // argv[argc] = argv_loc;
        put_user(argv_loc, &argv[argc]);
        argv_loc += len;
        assert(argv_loc < argv_max);
    }
    p->mm->arg_end = (uintptr_t)argv_loc;
    // argv[argc] = NULL;
    put_user(NULL, &argv[argc]);

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
        // strcpy(env_loc, p->info.envp[envc]);
        strncpy_to_user(env_loc, p->info.envp[envc], len);
        // envp[envc] = env_loc;
        put_user(env_loc, &envp[envc]);
        env_loc = env_loc + len;
        assert(env_loc < env_max);
    }
    p->mm->env_end = (uintptr_t)env_loc;
    // envp[envc] = NULL;
    put_user(NULL, &envp[envc]);

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

void start_process(void)
{
    sched_post_switch_unlock();
    struct mm_info *mem = current->mm;
    klog(LOG_DEBUG, "Process %d starting\n", current->pid);

    struct exec_info einfo = {0};
    if (load_executable(current, &einfo) < 0) {
        klog(LOG_ERROR, "Failed to load executable, exiting\n");
        exit(1);
    }

    mmap_write_lock(mem);
    struct vm_desc *desc = mem->mmap;
    while (desc) { // after the data segment, this seems imprecise?
        if ((desc->vm_flags & (VM_READ|VM_WRITE)) == (VM_READ|VM_WRITE))
            break;
        desc = desc->vm_next;
    }
    mem->start_brk = mem->brk = desc ? desc->end : 0;
    mem->start_stack = (uintptr_t)(__USER_STACK - __USER_STACK_SZ);
    set_vm_areas(mem);
    mmap_write_unlock(mem);

    unsigned long argc = count_task_vec(current->info.argv);
    unsigned long envc = count_task_vec(current->info.envp);

    // Copy argument and environment strings into brk area
    char *env_loc = (char*)mem->brk;
    char *arg_loc = env_loc + 0x1400;
    char *execfn_user = NULL;
    sbrk(0x4000);

    // TODO: replace with kernel entropy when available
    char *random_loc = arg_loc + 0xc00;
    static const u8 random_seed[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
    };
    if (copy_to_user(random_loc, random_seed, sizeof(random_seed))) {
        klog(LOG_WARN, "Failed to copy random seed to user space\n");
    }

    /*
     * Set up the initial user stack per the System V AMD64 ABI:
     *
     *   sp[0]                       = argc
     *   sp[1 .. argc]               = argv[0..argc-1]
     *   sp[argc+1]                  = NULL              (argv terminator)
     *   sp[argc+2 .. argc+1+envc]   = envp[0..envc-1]
     *   sp[argc+envc+2]             = NULL              (envp terminator)
     *   auxv pairs ...
     *   AT_NULL, 0
     *
     * musl's _start_c / _dlstart_c receive sp as their first argument.
     */
#define N_AUXV_PAIRS 14   /* number of AT_* pairs written below, excluding AT_NULL */
    size_t nslots = 1 + (argc + 1) + (envc + 1) + (N_AUXV_PAIRS + 1) * 2;
    uintptr_t *sp = (uintptr_t *)__USER_STACK - nslots;
    sp = (uintptr_t *)((uintptr_t)sp & ~(uintptr_t)0xf);

    // sp[0] = (uintptr_t)argc;
    put_user(argc, sp);

    char **argv_ptr = (char **)(sp + 1);
    char **envp_ptr = argv_ptr + argc + 1;

    if (current->info.argv && current->info.argv[0])
        execfn_user = arg_loc;

    if (!current->info.argv) {
        // argv_ptr[0] = NULL;
        put_user(NULL, &argv_ptr[0]);
    } else {
        prepare_task_args(current, argv_ptr, arg_loc, arg_loc + 0xc00);
    }

    if (!current->info.envp) {
        // envp_ptr[0] = NULL;
        put_user(NULL, &envp_ptr[0]);
    } else {
        prepare_task_env(current, envp_ptr, env_loc, arg_loc);
    }

    // Auxiliary vector — must end with AT_NULL pair
    uintptr_t *auxv = (uintptr_t *)(envp_ptr + envc + 1);
    int ai = 0;
#define AUXV_PAIR(type, val) do { \
    put_user((uintptr_t)(type), &auxv[ai]);     \
    put_user((uintptr_t)(val),  &auxv[ai + 1]); \
    ai += 2; \
} while (0)

    AUXV_PAIR(AT_PHDR,   einfo.at_phdr);
    AUXV_PAIR(AT_PHENT,  einfo.phentsize);
    AUXV_PAIR(AT_PHNUM,  einfo.phnum);
    AUXV_PAIR(AT_BASE,   einfo.interp_base);
    AUXV_PAIR(AT_ENTRY,  einfo.app_entry);
    AUXV_PAIR(AT_PAGESZ, PAGE_SIZE);
    AUXV_PAIR(AT_SECURE, 0);
    AUXV_PAIR(AT_RANDOM, random_loc);
    AUXV_PAIR(AT_EXECFN, execfn_user);
    AUXV_PAIR(AT_UID,    0);
    AUXV_PAIR(AT_EUID,   0);
    AUXV_PAIR(AT_GID,    0);
    AUXV_PAIR(AT_EGID,   0);
    AUXV_PAIR(AT_NULL,   0);
#undef AUXV_PAIR
#undef N_AUXV_PAIRS

    klog(LOG_DEBUG, "Going to user mode: entry=%p sp=%p argc=%lu argv=%p envp=%p\n",
         einfo.entry, sp, argc, argv_ptr, envp_ptr);
    klog(LOG_DEBUG, "  AT_PHDR=%p AT_BASE=%lx AT_ENTRY=%p\n",
         einfo.at_phdr, einfo.interp_base, einfo.app_entry);
    jump_usermode(einfo.entry, (void*)sp, current->kstack);
}


__noreturn
static void exec_and_return(void)
{
    klog(LOG_DEBUG, "Entering exec_and_return, pid = %d\n", current->pid);
    struct mm_info *old_mm = current->mm;
    struct mm_info *mem = alloc_mm_info();
    if (!mem) {
        panic("Failed to allocate memory for new mm_info\n");
    }
    struct task *task = current;
    mem->pgd = old_mm->pgd;

    task->mm = mem;
    task->pgd = mem->pgd;
    task->kstack = (void*)INIT_STACK(task->kstack_base);
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
    for (i = 0; argv[i] != NULL; i++) {
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
    for (i = 0; envp[i] != NULL; i++) {
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

    klog(LOG_DEBUG, "execve called with path = %s, argv = %p, envp = %p\n", path_buf, argv, envp);

    char **argv_buf = kmalloc(sizeof(char*) * 32);
    if (!argv_buf) {
        kfree(path_buf);
        return -ENOMEM;
    }
    char **envp_buf = kmalloc(sizeof(char*) * 32);
    if (!envp_buf) {
        kfree(path_buf);
        kfree(argv_buf);
        return -ENOMEM;
    }

    int i = 0;
    while (1) {
        char *argp = NULL;
        if (get_user(argp, &argv[i]) < 0) {
            err = -EFAULT;
            goto out;
        }
        argv_buf[i] = argp;
        if (!argp)
            break;
        i++;
        if (i >= 31) {
            err = -EINVAL;
            goto out;
        }
    }

    i = 0;
    while (1) {
        char *env = NULL;
        if (get_user(env, &envp[i]) < 0) {
            err = -EFAULT;
            goto out;
        }
        envp_buf[i] = env;
        if (!env)
            break;
        i++;
        if (i >= 31) {
            err = -EINVAL;
            goto out;
        }
    }

    err = do_execve(path_buf, argv_buf, envp_buf);
out:
    if (err < 0)
        kfree(path_buf);
    kfree(argv_buf);
    kfree(envp_buf);
    return err;
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
    dget(d);
    dput(task->fs->cwd_d);
    task->fs->cwd_d = d;
    release_lock(&task->fs->lock);
out:
    kfree(path_buf);
    return err;
}
