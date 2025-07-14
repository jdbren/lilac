// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _KERNEL_PROCESS_H
#define _KERNEL_PROCESS_H

#include <lilac/types.h>
#include <lilac/file.h>
#include <lilac/rbtree.h>

struct file;
struct regs_state;

#define TASK_RUNNING 0
#define TASK_SLEEPING 1
#define TASK_ZOMBIE 2

struct task_info {
    const char *path;
    char **argv;
    char **envp;
    struct file *exec_file;
};

struct fs_info {
    struct dentry *root_d;
    struct dentry *cwd_d;
    struct fdtable files;
};

struct task {
    int pid;
    int ppid;

    struct mm_info *mm;
    uintptr_t pgd;  // 12 (32-bit) or 16 (64-bit)
    uintptr_t pc;   // 16 (32-bit) or 24 (64-bit)
    void *kstack;   // 20 (32-bit) or 32 (64-bit)
    void *regs; // CPU registers
    void *fp_regs;

    spinlock_t lock;

    u8 state;
    u16 flags;
    u8 priority;
    u8 policy;

    u8 cpu;
    bool on_rq;
    u64 runtime;
    u64 timeslice;
    struct rb_node rq_node;

    struct task *parent;
    struct list_head children;
    struct list_head sibling;

    int exit_val;
    int exit_sig;

    bool parent_wait;
    bool waiting_any;
    struct fs_info fs;
    struct task_info info;

    char name[32];
};

struct mm_info {
    struct vm_desc *mmap;
    // struct rb_root mmap_rb;
    uintptr_t pgd;
    void *kstack;
    // atomic_uint ref_count;
    // u32 map_count;
    // struct semaphore mmap_sem;
    // spinlock_t page_table_lock;
    // struct list_head mmlist;
    uintptr_t start_code, end_code;
    uintptr_t start_data, end_data;
    uintptr_t brk;
    uintptr_t start_stack;
    // u32 arg_start, arg_end;
    // u32 env_start, env_end;
    u32 total_vm;
};

struct task *init_process(void);
u32 get_pid(void);
void reap_task(struct task *p);

// Architecture-specific functions
struct mm_info * arch_process_mmap(bool is_64_bit);
struct mm_info * arch_process_remap(struct mm_info *existing);
struct mm_info * arch_copy_mmap(struct mm_info *parent);
void             arch_unmap_all_user_vm(struct mm_info *info);
void             arch_reclaim_mem(struct task *p);
void *           arch_user_stack(void);
void *           arch_copy_regs(struct regs_state *src);
void             save_fp_regs(struct task *p);
void             restore_fp_regs(struct task *p);
void             copy_fp_regs(struct task *dst, struct task *src);

// kernel mode jump
void             jump_new_proc(struct task *next);
// user mode jumps
extern void      jump_usermode(void *addr, void *ustack, void *kstack);
extern int       arch_return_from_fork(void *regs, void *kstack);

#endif
