#include <lilac/syscall.h>
#include <lilac/sched.h>
#include <asm/regs.h>
#include <asm/gdt.h>

#ifdef __x86_64__
void x86_dump_regs(struct regs_state *regs)
{
    klog(LOG_DEBUG, "Register state:\n");
    klog(LOG_DEBUG, "  RAX: %016lx  RBX: %016lx  RCX: %016lx  RDX: %016lx\n",
         regs->ax, regs->bx, regs->cx, regs->dx);
    klog(LOG_DEBUG, "  RSI: %016lx  RDI: %016lx  RBP: %016lx  RSP: %016lx\n",
         regs->si, regs->di, regs->bp, regs->sp);
    klog(LOG_DEBUG, "  R8:  %016lx  R9:  %016lx  R10: %016lx  R11: %016lx\n",
         regs->r8, regs->r9, regs->r10, regs->r11);
    klog(LOG_DEBUG, "  R12: %016lx  R13: %016lx  R14: %016lx  R15: %016lx\n",
         regs->r12, regs->r13, regs->r14, regs->r15);
    klog(LOG_DEBUG, "  RIP: %016lx  CS:  %04lx   RFLAGS: %016lx\n",
         regs->ip, regs->cs, regs->flags);
}
#else
void x86_dump_regs(struct regs_state *regs)
{
    klog(LOG_DEBUG, "Register state:\n");
    klog(LOG_DEBUG, "  EAX: %08lx  EBX: %08lx  ECX: %08lx  EDX: %08lx\n",
         regs->ax, regs->bx, regs->cx, regs->dx);
    klog(LOG_DEBUG, "  ESI: %08lx  EDI: %08lx  EBP: %08lx  ESP: %08lx\n",
         regs->si, regs->di, regs->bp, regs->sp);
    klog(LOG_DEBUG, "  DS:  %04lx   ES:  %04lx   FS:  %04lx   GS:  %04lx\n",
         regs->ds, regs->es, regs->fs, regs->gs);
    klog(LOG_DEBUG, "  EIP: %08lx  CS:  %04lx   EFLAGS: %08lx\n",
         regs->ip, regs->cs, regs->flags);
}
#endif

int x86_kernel_entry(struct regs_state *regs)
{
    current->regs = (void*)regs;
    return 0;
}

int x86_kernel_exit(void)
{
    do_kernel_exit_work();
    if (current->flags.signaled) {
        current->flags.signaled = 0;
        return -1;
    }
    return 0;
}

void arch_prepare_context_switch(struct task *next)
{
    set_tss_esp0((uintptr_t)next->mm->kstack + __KERNEL_STACK_SZ);
}
