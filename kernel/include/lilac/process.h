// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _KERNEL_PROCESS_H
#define _KERNEL_PROCESS_H

#include <lilac/types.h>
#include <lilac/rwlock.h>
#include <lilac/signal.h>
#include <lib/hashtable.h>
#include <lilac/fdtable.h>
#include <lilac/timer_event.h>
#include <lib/rbtree.h>

struct regs_state;
struct file;
struct mm_info;

#define TASK_RUNNING 0
#define TASK_SLEEPING 1
#define TASK_ZOMBIE 2
#define TASK_STOPPED 3
#define TASK_UNINTERRUPTIBLE 4

struct task_info {
    const char *path;
    char **argv;
    char **envp;
    struct file *exec_file;
};

struct fs_info {
    struct dentry *root_d;
    struct dentry *cwd_d;
    atomic_uint ref_count;
    spinlock_t lock;
};

struct sighandlers {
    spinlock_t lock;
    atomic_uint ref_count;
    struct ksigaction actions[_NSIG];
};

struct restart_block {
    long (*fn)(struct restart_block *);
    ktime_t deadline;
    void __user *rem;
};

struct task_flags {
    u8 need_resched :1;
    u8 exiting      :1;
    u8 in_syscall   :1;
    u8 signaled     :1;
    u8 interrupted  :1;
    u8 state_change :1;
    u8 mm_last_ref  :1;
    u8 restore_sigmask :1;
};

struct task {
    int pid;
    int ppid;

    struct mm_info *mm;
    // Fields currently used in asm
    uintptr_t pgd;  // 12 (32-bit) or 16 (64-bit)
    uintptr_t pc;   // 16 (32-bit) or 24 (64-bit)
    void *kstack;   // 20 (32-bit) or 32 (64-bit)
    // ---
    void *regs;     // CPU registers
    void *fp_regs;  // Floating point / SIMD registers
    void *tls;      // Thread-local storage
    void *kstack_base;
    struct regs_state *reg_store; // heap allocated storage for regs

    spinlock_t lock;

    // Scheduling info
    u8 state;
    struct task_flags flags;
    u8 priority;
    u8 policy;
    u8 cpu;
    bool on_rq;
    bool on_cpu;
    u64 runtime;
    u64 vruntime;
    // u64 timeslice;
    struct rb_node rq_node;
    u64 exec_started;

    // Parent-child
    struct task *parent;
    struct list_head children;
    struct list_head sibling;
    struct task *tg_leader;
    struct hlist_node pid_hash;

    int pgid;
    int sid;
    struct hlist_node pgid_hash;
    struct hlist_node sid_hash;

    pid_t tgid;     // Thread group ID
    int exit_signal;
    int exit_status;
    pid_t *set_child_tid;
    pid_t *clear_child_tid;
    struct waitqueue *vfork_done;

    int group_exit_code;
    bool group_exit;

    struct fs_info *fs;
    struct fdtable *files;

    struct sighandlers *sighand;
    _Atomic sigset_t pending;
    sigset_t blocked;

    struct tty *ctty;

    struct list_head timer_ev_list;
    struct {
        struct timer_event ev;  // ITIMER_REAL
        ktime_t interval;
    } itimer_real;

    struct task_info info;
    char name[32];

    long syscall_nr;
    struct restart_block restart_block;
    sigset_t saved_sigmask;
    struct ksiginfo siginfo[_NSIG];
};

#define get_pid() (current->pid)

struct task *init_process(void);
void reap_task(struct task *p);
__noreturn void do_exit(void);
struct task * get_task_by_pid(int pid);
struct task * get_pgrp_leader(int pgid);
struct task * get_any_pgrp_member(pid_t pgid);
int is_current_pgrp_orphaned(void);

// Architecture-specific functions
void             arch_pre_context_switch(struct task *prev, struct task *next);
void             arch_post_context_switch(struct task *p);
struct mm_info * arch_process_mmap(bool is_64_bit);
struct mm_info * arch_copy_mmap(struct mm_info *parent);
void             arch_unmap_all_user_vm(struct mm_info *info);
void             arch_reclaim_mem(struct task *p);
void             arch_free_old_pgd(struct mm_info *old);
void *           arch_user_stack(void);
void *           arch_get_user_sp(void);
void             arch_set_user_sp(struct task *p, void *sp);
struct regs_state *alloc_regs_state(void);
void *           arch_copy_regs(struct regs_state *dst, struct regs_state *src);
void             save_fp_regs(struct task *p);
void             restore_fp_regs(struct task *p);
void             copy_fp_regs(struct task *dst, struct task *src);
void             fpu_switch(struct task *prev, struct task *next);
const void *     fpu_sigframe_state(struct task *p);
void             fpu_sigframe_reset(struct task *p);
void *           fpu_sigframe_restore_buf(struct task *p);
void             fpu_sigframe_restore_fixup(struct task *p);
void             arch_prepare_signal(void *pc, int signo, const struct ksiginfo *info,
                                     void *restorer);
void             arch_restart_syscall(struct task *p, bool has_handler, bool sa_restart);
long             arch_restore_post_signal(void);

// kernel mode jump
void             jump_new_proc(struct task *next);
// user mode jumps
extern void      jump_usermode(void *addr, void *ustack, void *kstack);
extern int       arch_return_from_fork(void *regs, void *kstack, void *tls);

#define INIT_STACK(KSTACK) ((uintptr_t)KSTACK + __KERNEL_STACK_SZ - sizeof(size_t))

#define PID_HASH_BITS 12

extern DECLARE_HASHTABLE(pid_table, PID_HASH_BITS);
extern DECLARE_HASHTABLE(pgid_table, PID_HASH_BITS);
extern DECLARE_HASHTABLE(sid_table, PID_HASH_BITS);

// Protects the pid/pgid/sid hash tables and every task's children list
extern spinlock_t tasklist_lock;

#define pgrp_for_each(p, pgid) hash_for_each_possible(pgid_table, p, pgid_hash, pgid) \
    if (p->pgid == pgid)

#endif
