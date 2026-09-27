#include <lilac/clone.h>
#include <lilac/fs.h>
#include <lilac/futex.h>
#include <lilac/lilac.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/syscall.h>
#include <lilac/uaccess.h>
#include <lilac/wait.h>
#include <lilac/timer_event.h>
#include <mm/page.h>

static atomic_int num_tasks = 1;

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
    spin_lock_init(&sh->lock);
    return sh;
}

struct fs_info * alloc_fs_info(void)
{
    struct fs_info *fs = kzmalloc(sizeof(*fs));
    if (!fs) return NULL;
    fs->ref_count = 1;
    spin_lock_init(&fs->lock);
    return fs;
}


static void copy_fs_info(struct fs_info *dst, struct fs_info *src)
{
    dget(src->root_d);
    dget(src->cwd_d);
    dst->root_d = src->root_d;
    dst->cwd_d = src->cwd_d;
}

static int copy_files(struct fdtable *dst, struct fdtable *src)
{
    if (dst->fdarray)
        kfree(dst->fdarray);
    dst->fdarray = kcalloc(src->max, sizeof(struct file*));
    if (dst->fdarray == NULL)
        return -ENOMEM;
    dst->max = src->max;
    for (size_t i = 0; i < src->max; i++) {
        dst->fdarray[i] = src->fdarray[i];
        if (dst->fdarray[i]) {
            fget(dst->fdarray[i]);
        }
    }
    memcpy(dst->close_on_exec, src->close_on_exec,
        BITS_TO_LONGS(src->max) * sizeof(unsigned long));
    return 0;
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


struct task *init_process(void)
{
    extern void start_process(void);
    struct task *this = kzmalloc(sizeof(*this));
    struct mm_info *mem = arch_process_mmap(sizeof(void*) == 8);

    this->pid = 1;
    this->ppid = 0;
    this->pgid = this->pid;
    this->sid = this->pid;
    this->tgid = this->pid;
    this->tg_leader = this;
    this->tg_live = 1;
    this->exit_signal = SIGCHLD;
    this->mm = mem;
    this->pgd = mem->pgd;
    this->kstack_base = alloc_kstack();
    this->kstack = (void*)INIT_STACK(this->kstack_base);
    this->pc = (uintptr_t)(start_process);
    this->state = TASK_RUNNING;
    this->fs = alloc_fs_info();
    this->files = alloc_fdtable(8);
    this->fs->root_d = get_root_dentry();
    this->fs->cwd_d = this->fs->root_d;
    dget(this->fs->root_d);
    dget(this->fs->cwd_d);
    this->info.path = strdup("/sbin/init");
    this->info.argv = kcalloc(2, sizeof(char*));
    this->info.argv[0] = strdup("init");
    this->info.envp = kcalloc(1, sizeof(char*));
    this->sighand = alloc_sighandlers();
    INIT_LIST_HEAD(&this->children);
    INIT_LIST_HEAD(&this->dead_node);
    INIT_LIST_HEAD(&this->sibling);
    hash_add(pid_table, &this->pid_hash, this->pid);
    hash_add(pgid_table, &this->pgid_hash, this->pgid);
    hash_add(sid_table, &this->sid_hash, this->sid);
    INIT_LIST_HEAD(&this->timer_ev_list);
    init_itimer_real(this);
    init_rlimits(this->rlim);

    return this;
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
    INIT_LIST_HEAD(&child->dead_node);
    child->on_rq = false;
    child->on_cpu = false;
    child->flags.mm_last_ref = 0;
    child->syscall_nr = -1;
    child->restart_block.fn = NULL;
    child->group_exit = false;
    child->flags.restore_sigmask = 0;
    child->group_exit_code = 0;

    child->pid = ++num_tasks;
    if (flags & CLONE_THREAD) {
        child->tgid = cur->tgid;
        child->tg_leader = cur->tg_leader;
        child->tg_live = 0;
        atomic_fetch_add(&cur->tg_leader->tg_live, 1);
    } else {
        child->tgid = child->pid;
        child->tg_leader = child;
        child->tg_live = 1;
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

    unsigned long tl_flags;
    acquire_write_lock_irqsave(&tasklist_lock, &tl_flags);
    list_add_tail(&child->sibling, &parent->children);
    hash_add(pid_table, &child->pid_hash, child->pid);
    hash_add(pgid_table, &child->pgid_hash, child->pgid);
    hash_add(sid_table, &child->sid_hash, child->sid);
    release_write_lock_irqrestore(&tasklist_lock, tl_flags);

    child->kstack_base = alloc_kstack();
    if (!child->kstack_base) {
        panic("Failed to allocate kernel stack for child process\n");
    }
    if (flags & CLONE_VM) {
        cur->mm->ref_count++;
    } else {
        child->mm = arch_copy_mmap(cur->mm);
    }
    child->pgd = child->mm->pgd;
    child->kstack = (void*)INIT_STACK(child->kstack_base);

    child->reg_store = alloc_regs_state();
    if (!child->reg_store)
        panic("Failed to allocate regs_state for child process\n");
    child->regs = arch_copy_regs(child->reg_store, cur->regs);
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
    init_itimer_real(child);
    memcpy(child->rlim, cur->tg_leader->rlim, sizeof(child->rlim));

    return child;
}

static void return_from_fork(void)
{
    struct task *p;
    sched_post_switch_unlock();
    p = current;
    if (p->set_child_tid) {
        klog(LOG_DEBUG, "PID %d: Setting child TID at %p\n", p->pid, p->set_child_tid);
        if (put_user(p->pid, p->set_child_tid)) {
            klog(LOG_DEBUG, "PID %d: Failed to set child TID\n", p->pid);
            do_raise(p, SIGSEGV);
        }
    }
    klog(LOG_DEBUG, "PID %d: Returning from fork\n", p->pid);
    arch_return_from_fork(p->regs, p->kstack, p->tls);
}

static void wait_for_vfork_done(struct task *p, struct waitqueue *wq)
{
    wait_event_uninterruptible(*wq, READ_ONCE(p->vfork_done) == NULL);
}

int do_clone(struct clone_args *args)
{
    struct waitqueue vfork_wait = WAITQUEUE_INIT(vfork_wait);
    unsigned long flags = args->flags;

    klog(LOG_DEBUG, "PID %d: Cloning process with flags 0x%lx\n",
        current->pid, flags);

    if ((flags & CLONE_PARENT_SETTID) &&
            !access_ok(args->parent_tid, sizeof(pid_t))) {
        klog(LOG_DEBUG, "PID %d: Invalid parent_tid pointer %p\n",
            current->pid, args->parent_tid);
        return -EFAULT;
    }

    reap_dead_threads();
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
    if (flags & CLONE_VFORK) {
        child->vfork_done = &vfork_wait;
    }

    schedule_task(child);

    if (flags & CLONE_VFORK) {
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

SYSCALL_DECL5(clone, unsigned long, flags, void*, stack, void*, ptid,
    void*, ctid, unsigned long, tls)
{
    if (flags & CLONE_THREAD) {
        if ((flags & (CLONE_VM|CLONE_SIGHAND)) != (CLONE_VM|CLONE_SIGHAND))
            return -EINVAL;
    }

    struct clone_args args = {
        .flags = flags & ~0xfful,
        .stack = stack,
        .parent_tid = ptid,
        .child_tid = ctid,
        .tls = (void*)tls,
        .exit_signal = flags & 0xff ? flags & 0xff : SIGCHLD,
    };

    int pid = do_clone(&args);

    return pid;
}


// Returns true if this dropped the last reference to mm
static bool mm_release(struct task *p, struct mm_info *mm)
{
    if (p->clear_child_tid) {
        put_user(0, p->clear_child_tid);
        // do_futex(p->clear_child_tid, FUTEX_WAKE, 1, NULL, NULL, 0, 0);
        futex_wake(p->clear_child_tid, 1);
        p->clear_child_tid = NULL;
    }

    struct waitqueue *vfork_wq = p->vfork_done;
    if (vfork_wq) {
        unsigned long flags;
        // The waitqueue is on the parent's stack. Clear the flag and wake under
        // its lock: the parent can't leave end_wait (which takes the lock)
        // until we have released it and stopped touching the queue.
        acquire_lock_irqsave(&vfork_wq->lock, &flags);
        WRITE_ONCE(p->vfork_done, NULL);
        __wake_all(vfork_wq);
        release_lock_irqrestore(&vfork_wq->lock, flags);
    }

    if (--mm->ref_count == 0) {
        arch_unmap_all_user_vm(mm);
        return true;
    }
    return false;
}

void exit_mm_release(struct task *tsk, struct mm_info *mm)
{
    // futex_exit_release(tsk);
    tsk->flags.mm_last_ref = mm_release(tsk, mm);
}

bool exec_mm_release(struct task *tsk, struct mm_info *mm)
{
    // futex_exec_release(tsk);
    return mm_release(tsk, mm);
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
        kfree(files->close_on_exec);
        kfree(files);
    }

    if (!--fs->ref_count) {
        dput(fs->cwd_d);
        dput(fs->root_d);
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
    if (info->envp) {
        for (int i = 0; info->envp[i]; i++)
            kfree(info->envp[i]);
        kfree(info->envp);
        info->envp = NULL;
    }
}

static void cleanup_memory(struct mm_info *mm)
{
    klog(LOG_DEBUG, "Cleaning up memory\n");
    exit_mm_release(current, mm);
    // mm_struct is freed in reap_task() since we need original kstack
}

void cleanup_task(struct task *p)
{
    klog(LOG_DEBUG, "Cleaning up task %d\n", p->pid);
    timer_ev_task_exit(p);
    cleanup_task_info(&p->info);
    cleanup_fs(p->fs, p->files);
    cleanup_memory(p->mm);
}

void reap_task(struct task *p)
{
    if (p->state != TASK_ZOMBIE) {
        klog(LOG_WARN, "Tried to reap task %d, but it is not dead\n", p->pid);
        return;
    }
    klog(LOG_DEBUG, "Reaping task %d\n", p->pid);
    // The child marks itself a zombie before its final schedule(), so it may
    // still be running on its kernel stack; wait until it has switched away
    while (__atomic_load_n(&p->on_cpu, __ATOMIC_ACQUIRE))
        __pause();
    kfree(p->fp_regs);
    p->fp_regs = NULL;
    unsigned long flags;
    acquire_write_lock_irqsave(&tasklist_lock, &flags);
    list_del(&p->sibling);
    hash_del(&p->pid_hash);
    hash_del(&p->pgid_hash);
    hash_del(&p->sid_hash);
    release_write_lock_irqrestore(&tasklist_lock, flags);
    put_sighandlers(p->sighand);
    kfree(p->reg_store);
    free_pages(p->kstack_base, __KERNEL_STACK_SZ / PAGE_SIZE);
    if (p->flags.mm_last_ref) {
        arch_reclaim_mem(p);
        kfree(p->mm);
    }
    kfree(p);
}

// TODO: kworker threads
static LIST_HEAD(dead_threads);
static spinlock_t dead_threads_lock = SPINLOCK_INIT;

static void queue_dead_thread(struct task *p)
{
    unsigned long flags;
    acquire_lock_irqsave(&dead_threads_lock, &flags);
    list_add_tail(&p->dead_node, &dead_threads);
    release_lock_irqrestore(&dead_threads_lock, flags);
}

void reap_dead_threads(void)
{
    struct task *p;
    unsigned long flags;

    for (;;) {
        acquire_lock_irqsave(&dead_threads_lock, &flags);
        p = list_first_entry_or_null(&dead_threads, struct task, dead_node);
        if (p)
            list_del_init(&p->dead_node);
        release_lock_irqrestore(&dead_threads_lock, flags);
        if (!p)
            break;
        reap_task(p);
    }
}

static void reparent_children(struct task *p)
{
    struct task *init = get_task_by_pid(1);
    struct task *child, *tmp, *zombie = NULL;
    unsigned long flags;

    if (!init || init == p)
        return;

    acquire_write_lock_irqsave(&tasklist_lock, &flags);
    list_for_each_entry_safe(child, tmp, &p->children, sibling) {
        list_del(&child->sibling);
        child->parent = init;
        child->ppid = init->pid;
        list_add_tail(&child->sibling, &init->children);
        if (READ_ONCE(child->state) == TASK_ZOMBIE &&
                (!zombie || zombie->exit_signal <= 0))
            zombie = child;
    }
    release_write_lock_irqrestore(&tasklist_lock, flags);

    if (zombie)
        notify_parent(init, zombie);
}

__noreturn void do_exit(void)
{
    struct task *parent = NULL;
    unsigned long flags;
    if (unlikely(current->pid <= 1))
        panic("Init or kernel tried to exit!\n");
    reap_dead_threads();
    reparent_children(current);
    cleanup_task(current);
    set_current_state(TASK_ZOMBIE);

    struct task *leader = current->tg_leader;
    if (current != leader)
        queue_dead_thread(current);

    acquire_read_lock_irqsave(&tasklist_lock, &flags);
    bool group_dead = atomic_fetch_sub(&leader->tg_live, 1) == 1;
    parent = current->parent;
    if (parent && group_dead)
        notify_parent(parent, leader);
    release_read_lock_irqrestore(&tasklist_lock, flags);

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

SYSCALL_DECL1(exit_group, int, status)
{
    zap_other_threads(current, WEXITED(status));
    exit(status);
    return -1;
}
