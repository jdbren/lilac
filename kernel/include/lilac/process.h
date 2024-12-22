// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _KERNEL_PROCESS_H
#define _KERNEL_PROCESS_H

#include <lilac/types.h>
#include <lilac/file.h>

#define TASK_RUNNING 0
#define TASK_SLEEPING 1
#define TASK_DEAD 2

struct task_info {
    const char *path;
    char **argv;
    char **envp;
};

struct fs_info {
    char *root;
    char *cwd;
    struct fdtable files;
};

struct task {
    u16 pid;
    u16 ppid;
    struct mm_info *mm;
    uintptr_t pgd; // 8
    uintptr_t pc; // 12
    void *stack; // 16
    u32 time_slice;
    struct task *parent;
    struct fs_info fs;
    struct task_info info;
    u8 priority;
    volatile u8 state;
    char name[32];
    void *regs;
};

struct mm_info {
    struct vm_desc *mmap;
    // struct rb_tree mmap_rb;
    uintptr_t pgd;
    void *kstack;
    // atomic_uint ref_count;
    // u32 map_count;
    // struct semaphore mmap_sem;
    // spinlock_t page_table_lock;
    // struct list_head mmlist;
    uintptr_t start_code, end_code;
    uintptr_t start_data, end_data;
    uintptr_t start_brk, brk;
    uintptr_t start_stack;
    // u32 arg_start, arg_end;
    // u32 env_start, env_end;
    u32 total_vm;
};

struct task *init_process(void);
struct task *create_process(const char *path);
u32 get_pid(void);

struct mm_info *arch_process_mmap();
void *arch_user_stack(void);
extern void jump_usermode(u32 addr, u32 stack);
extern int arch_return_from_fork(void *regs);
void *arch_copy_regs(void *src);
struct mm_info *arch_copy_mmap(struct mm_info *parent);

#endif
