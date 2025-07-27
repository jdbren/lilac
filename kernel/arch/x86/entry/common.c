#include <lilac/syscall.h>
#include <lilac/sched.h>
#include "gdt.h"

void x86_kernel_exit(void)
{
    do_kernel_exit_work();
}

void arch_prepare_context_switch(struct task *next)
{
    set_tss_esp0((uintptr_t)next->mm->kstack + __KERNEL_STACK_SZ);
}
