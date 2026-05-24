#include <lilac/clone.h>
#include <lilac/fs.h>
#include <lilac/lilac.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/syscall.h>
#include <lilac/uaccess.h>
#include <lilac/wait.h>
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
    INIT_LIST_HEAD(&this->sibling);
    hash_add(pid_table, &this->pid_hash, this->pid);
    hash_add(pgid_table, &this->pgid_hash, this->pgid);
    hash_add(sid_table, &this->sid_hash, this->sid);
    INIT_LIST_HEAD(&this->timer_ev_list);

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
        if (put_user(p->pid, p->set_child_tid)) {
            klog(LOG_DEBUG, "PID %d: Failed to set child TID\n", p->pid);
            do_raise(p, SIGSEGV);
        }
    }
    klog(LOG_DEBUG, "PID %d: Returning from fork\n", p->pid);
    arch_return_from_fork(p->regs, p->kstack);
}

static void wait_for_vfork_done(struct task *p, struct waitqueue *wq)
{
    sleep_on(wq);
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


static void mm_release(struct task *p, struct mm_info *mm)
{
    if (p->clear_child_tid) {
        put_user(0, p->clear_child_tid);
        // do_futex(p->clear_child_tid, FUTEX_WAKE, 1, NULL, NULL, 0, 0);
        p->clear_child_tid = NULL;
    }

    if (p->vfork_done) {
        wake_all(p->vfork_done);
        p->vfork_done = NULL;
    }

    if (!--mm->ref_count)
        arch_unmap_all_user_vm(mm);
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
    cleanup_task_info(&p->info);
    cleanup_fs(p->fs, p->files);
    cleanup_memory(p->mm);
    put_sighandlers(p->sighand);
}

// TODO: race conditions related to reaping and tasks
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
    free_pages(p->kstack_base, __KERNEL_STACK_SZ / PAGE_SIZE);
    if (p->mm->ref_count == 0) {
        arch_reclaim_mem(p);
        kfree(p->mm);
    }
}

__noreturn void do_exit(void)
{
    struct task *parent = NULL;
    if (unlikely(current->pid <= 1))
        panic("Init or kernel tried to exit!\n");
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
