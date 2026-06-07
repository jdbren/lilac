#include <lilac/syscall.h>
#include <lilac/sched.h>
#include <asm/regs.h>
#include <asm/gdt.h>
#include <asm/msr.h>

#ifdef __x86_64__
void x86_dump_regs(struct regs_state *regs)
{
    klog(LOG_DEBUG, "================== Register state ==================\n");
    klog(LOG_DEBUG, "task: %p\n", current);
    klog(LOG_DEBUG, "RIP: %04lx:%016lx RSP: %04lx:%016lx EFLAGS: %08lx\n",
        regs->cs, regs->ip, regs->ss, regs->sp, regs->flags);
    klog(LOG_DEBUG, "RAX: %016lx RBX: %016lx RCX: %016lx\n", regs->ax, regs->bx, regs->cx);
    klog(LOG_DEBUG, "RDX: %016lx RSI: %016lx RDI: %016lx\n", regs->dx, regs->si, regs->di);
    klog(LOG_DEBUG, "RBP: %016lx  R8: %016lx  R9: %016lx\n", regs->bp, regs->r8, regs->r9);
    klog(LOG_DEBUG, "R10: %016lx R11: %016lx R12: %016lx\n", regs->r10, regs->r11, regs->r12);
    klog(LOG_DEBUG, "R13: %016lx R14: %016lx R15: %016lx\n", regs->r13, regs->r14, regs->r15);
    klog(LOG_DEBUG, "FS:  %016lx GS:  %016lx KGS: %016lx\n",
        rdmsr(IA32_FS_BASE), rdmsr(IA32_GS_BASE), rdmsr(IA32_KERNEL_GS_BASE));
    klog(LOG_DEBUG, "CR0: %016lx CR2: %016lx CR3: %016lx CR4: %016lx\n",
        read_cr0(), read_cr2(), read_cr3(), read_cr4());
    klog(LOG_DEBUG, "====================================================\n");
}

void x86_print_stack_trace(struct regs_state *regs)
{
    klog(LOG_DEBUG, "Call trace:\n");
    uintptr_t *stack = (uintptr_t*)regs->bp;
    for (int i = 0; i < 16 && stack && stack[1]; i++) {
        klog(LOG_DEBUG, "  %p\n", (void*)stack[1]);
        stack = (uintptr_t*)stack[0];
    }
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

#ifdef DEBUG_ENTRY
void x86_debug_syscall_entry(struct regs_state *regs)
{
    klog(LOG_DEBUG, "Syscall entry: RAX = %ld\n", (long) regs->ax);
}

void x86_debug_syscall_exit(struct regs_state *regs)
{
    klog(LOG_DEBUG, "Syscall exit: RAX = %ld\n", (long) regs->ax);
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

void arch_pre_context_switch(struct task *prev, struct task *next)
{
    set_tss_esp0((uintptr_t)next->kstack_base + __KERNEL_STACK_SZ);
    prev->tls = (void*)(uintptr_t)rdmsr(IA32_FS_BASE);
}

void arch_post_context_switch(struct task *p)
{
    wrmsr(IA32_FS_BASE, (uintptr_t)p->tls);
}
