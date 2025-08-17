#include <lilac/syscall.h>
#include <lilac/sched.h>
#include <asm/regs.h>
#include "gdt.h"

int x86_kernel_entry(struct regs_state *regs)
{
    current->regs = (void*)regs;
    return 0;
}

void x86_kernel_exit(void)
{
    do_kernel_exit_work();
}

void arch_prepare_context_switch(struct task *next)
{
    set_tss_esp0((uintptr_t)next->mm->kstack + __KERNEL_STACK_SZ);
}
