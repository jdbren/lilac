#include <stdatomic.h>
#include <kernel/process.h>
#include <kernel/types.h>
#include <kernel/panic.h>
#include <kernel/elf.h>
#include <mm/kheap.h>
#include <fs/vfs.h>

#define MAX_TASKS 1024

struct task {
    int pid;
    int ppid;
    int page_dir;
    volatile int state;
    int pc;
    void *stack;
    int priority;
    unsigned int time_slice;
    struct task *parent;
    struct fs_info *fs;
    struct files *files;
};

static struct task tasks[MAX_TASKS];
static int task_queue;
static int num_tasks;

void start_process()
{
    printf("Process started\n");
    while (1)
        asm("hlt");
    struct task *task = &tasks[task_queue];
}

void context_switch(struct task *prev, struct task *next)
{
    asm ("movl %0, %%cr3" : : "r"(next->page_dir));
    asm ("movl $0xC80001FC, %esp");
    asm ("pushl %0" : : "r"(next->pc));
    asm ("ret");

    asm volatile (
        "pushfl\n\t"
        "pushl %%ebp\n\t"
        "movl %%esp, %[prev_sp]\n\t"
        "movl %[next_sp], %%esp\n\t"
        "movl $1f, %[prev_ip]\n\t"
        "pushl %[next_ip]\n\t"
        "jmp __switch_to\n"
        "1:\t"
        "popl %%ebp\n\t"
        "popfl\n\t"
        : [prev_sp] "=m" (prev->stack),
          [prev_ip] "=m" (prev->pc)
        : [next_sp] "m" (next->stack),
          [next_ip] "m" (next->pc),
          [prev]    "a" (prev),
          [next]    "d" (next)
        : "memory"
    );

}

void create_process()
{
    static int pid = 0;
    struct task *task = &tasks[++pid];
    // Allocate new page directory
    u32 directory = arch_process_mmap();
    printf("Page directory: %x\n", directory);

    task->pid = pid;
    task->ppid = 0;
    task->state = 0;
    task->page_dir = directory;
    task->pc = (u32)(start_process);
    printf("Process entry point: %x\n", task->pc);

    context_switch(NULL, task);

    num_tasks++;
    return;
}

