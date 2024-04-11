// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _KERNEL_PROCESS_H
#define _KERNEL_PROCESS_H

#include <lilac/types.h>

struct task_info {
    const char *path;
    char **argv;
    char **envp;
};

struct task {
    u16 pid;
    u16 ppid;
    u32 pgd;
    u32 pc;
    void *stack;
    u32 time_slice;
    struct task *parent;
    struct fs_info *fs;
    struct files *files;
    struct task_info *info;
    u8 priority;
    volatile u8 state;
    char name[16];
};

struct mm_info {
    u32 pgd;
    void *kstack;
};

struct task *init_process(void);
struct task *create_process(const char *path);
u32 get_pid(void);

struct mm_info arch_process_mmap();
void arch_user_stack();
extern void jump_usermode(u32 addr, u32 stack);

#endif
