#ifndef _KERNEL_PROCESS_H
#define _KERNEL_PROCESS_H

#include <kernel/types.h>

struct task_info {
    char *path;
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
struct mm_info arch_process_mmap();

#endif
