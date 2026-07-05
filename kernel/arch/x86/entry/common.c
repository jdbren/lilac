#include <lilac/syscall.h>
#include <lilac/sched.h>
#include <lilac/symbols.h>
#include <asm/regs.h>
#include <asm/gdt.h>
#include <asm/msr.h>

#ifdef __x86_64__
void x86_dump_regs(struct regs_state *regs)
{
    printf("==================== Register state ====================\n");
    printf("task: %p\n", current);
    printf("RIP: %04lx:%016lx RSP: %04lx:%016lx EFLAGS: %08lx\n",
        regs->cs, regs->ip, regs->ss, regs->sp, regs->flags);
    printf("RAX: %016lx RBX: %016lx RCX: %016lx\n", regs->ax, regs->bx, regs->cx);
    printf("RDX: %016lx RSI: %016lx RDI: %016lx\n", regs->dx, regs->si, regs->di);
    printf("RBP: %016lx  R8: %016lx  R9: %016lx\n", regs->bp, regs->r8, regs->r9);
    printf("R10: %016lx R11: %016lx R12: %016lx\n", regs->r10, regs->r11, regs->r12);
    printf("R13: %016lx R14: %016lx R15: %016lx\n", regs->r13, regs->r14, regs->r15);
    printf(" FS: %016lx  GS: %016lx KGS: %016lx\n",
        rdmsr(IA32_FS_BASE), rdmsr(IA32_GS_BASE), rdmsr(IA32_KERNEL_GS_BASE));
    printf("CR0: %016lx CR2: %016lx CR3: %016lx\n",
        read_cr0(), read_cr2(), read_cr3());
    printf("CR4: %016lx EFER: %016lx\n", read_cr4(), read_efer());
    printf("========================================================\n");
}

void x86_print_stack_trace(struct regs_state *regs)
{
    printf("Call trace:\n");
    uintptr_t low = (uintptr_t)current->kstack_base;
    uintptr_t high = low + __KERNEL_STACK_SZ;
    uintptr_t *stack = (uintptr_t*)regs->bp;

    // Print current instruction pointer first
    uintptr_t sym_addr = 0;
    const char *sym_name = ksym_lookup(regs->ip, &sym_addr);
    if (sym_name && regs->ip >= sym_addr) {
        printf("  0x%p <%s+0x%lx>\n",
            (void*)regs->ip, sym_name, (unsigned long)(regs->ip - sym_addr));
    } else {
        printf("  0x%p\n", (void*)regs->ip);
    }

    for (int i = 0; i < 16; i++) {
        if (!stack || (uintptr_t)stack < low || (uintptr_t)(stack + 1) >= high || !stack[1])
            break;
        uintptr_t ret_addr = stack[1];
        uintptr_t sym_addr = 0;
        const char *sym_name = ksym_lookup(ret_addr, &sym_addr);

        if (sym_name && ret_addr >= sym_addr) {
            printf("  0x%p <%s+0x%lx>\n",
                (void*)ret_addr, sym_name, (unsigned long)(ret_addr - sym_addr));
        } else {
            printf("  0x%p\n", (void*)ret_addr);
        }

        stack = (uintptr_t*)stack[0];
    }
}

void arch_panic_dump_regs(void)
{
    struct regs_state *regs = current ? (struct regs_state*)current->regs : NULL;

    if (regs) {
        printf("==================== Register state ====================\n");
        printf("task: %p\n", current);
        printf("RIP: %04lx:%016lx RSP: %04lx:%016lx EFLAGS: %08lx\n",
            regs->cs, regs->ip, regs->ss, regs->sp, regs->flags);
        printf("RAX: %016lx RBX: %016lx RCX: %016lx\n", regs->ax, regs->bx, regs->cx);
        printf("RDX: %016lx RSI: %016lx RDI: %016lx\n", regs->dx, regs->si, regs->di);
        printf("RBP: %016lx  R8: %016lx  R9: %016lx\n", regs->bp, regs->r8, regs->r9);
        printf("R10: %016lx R11: %016lx R12: %016lx\n", regs->r10, regs->r11, regs->r12);
        printf("R13: %016lx R14: %016lx R15: %016lx\n", regs->r13, regs->r14, regs->r15);
        printf(" FS: %016lx  GS: %016lx KGS: %016lx\n",
            rdmsr(IA32_FS_BASE), rdmsr(IA32_GS_BASE), rdmsr(IA32_KERNEL_GS_BASE));
        printf("CR0: %016lx CR2: %016lx CR3: %016lx\n",
            read_cr0(), read_cr2(), read_cr3());
        printf("CR4: %016lx EFER: %016lx\n", read_cr4(), read_efer());
        printf("========================================================\n");
        return;
    }

    unsigned long ip = (unsigned long)__builtin_return_address(0);
    unsigned long bp = (unsigned long)__builtin_frame_address(0);
    unsigned long sp = 0;
    unsigned long flags = 0;

    asm volatile("mov %%rsp, %0" : "=r"(sp));
    asm volatile("pushfq; pop %0" : "=r"(flags));

    printf("==================== Register state ====================\n");
    printf("task: %p\n", current);
    printf("RIP: %016lx RSP: %016lx RBP: %016lx EFLAGS: %08lx\n",
        ip, sp, bp, flags);
    printf("CR0: %016lx CR2: %016lx CR3: %016lx\n",
        read_cr0(), read_cr2(), read_cr3());
    printf("CR4: %016lx CR8: %016lx\n", read_cr4(), read_cr8());
    printf("========================================================\n");
}

void arch_panic_stack_trace(void)
{
    uintptr_t low = (uintptr_t)current->kstack_base;
    uintptr_t high = low + __KERNEL_STACK_SZ;
    uintptr_t bp = 0;
    uintptr_t ip = 0;

    struct regs_state *regs = (struct regs_state*)current->regs;
    if (regs) {
        bp = regs->bp;
        ip = regs->ip;
    }

    if (!bp || bp < low || bp >= high)
        asm volatile("mov %%rbp, %0" : "=r"(bp));

    if (!ip)
        ip = (uintptr_t)__builtin_return_address(0);

    printf("Call trace:\n");

    // Print the faulting instruction first
    uintptr_t sym_addr = 0;
    const char *sym_name = ksym_lookup(ip, &sym_addr);
    if (sym_name && ip >= sym_addr) {
        printf("  0x%p <%s+0x%lx>\n",
            (void*)ip, sym_name, (unsigned long)(ip - sym_addr));
    } else {
        printf("  0x%p\n", (void*)ip);
    }

    uintptr_t *stack = (uintptr_t*)bp;
    for (int i = 0; i < 16; i++) {
        if (!stack || (uintptr_t)stack < low || (uintptr_t)(stack + 1) >= high || !stack[1])
            break;

        uintptr_t ret_addr = stack[1];
        uintptr_t sym_addr = 0;
        const char *sym_name = ksym_lookup(ret_addr, &sym_addr);

        if (sym_name && ret_addr >= sym_addr) {
            printf("  0x%p <%s+0x%lx>\n",
                (void*)ret_addr, sym_name, (unsigned long)(ret_addr - sym_addr));
        } else {
            printf("  0x%p\n", (void*)ret_addr);
        }

        stack = (uintptr_t*)stack[0];
    }
}
#else
void x86_dump_regs(struct regs_state *regs)
{
    printf("Register state:\n");
    printf("  EAX: %08lx  EBX: %08lx  ECX: %08lx  EDX: %08lx\n",
         regs->ax, regs->bx, regs->cx, regs->dx);
    printf("  ESI: %08lx  EDI: %08lx  EBP: %08lx  ESP: %08lx\n",
         regs->si, regs->di, regs->bp, regs->sp);
    printf("  DS:  %04lx   ES:  %04lx   FS:  %04lx   GS:  %04lx\n",
         regs->ds, regs->es, regs->fs, regs->gs);
    printf("  EIP: %08lx  CS:  %04lx   EFLAGS: %08lx\n",
         regs->ip, regs->cs, regs->flags);
}

void arch_panic_stack_trace(void)
{
}

void arch_panic_dump_regs(void)
{
    struct regs_state *regs = current ? (struct regs_state*)current->regs : NULL;

    if (!regs) {
       return;
    }

    printf("Register state:\n");
    printf("  EAX: %08lx  EBX: %08lx  ECX: %08lx  EDX: %08lx\n",
        regs->ax, regs->bx, regs->cx, regs->dx);
    printf("  ESI: %08lx  EDI: %08lx  EBP: %08lx  ESP: %08lx\n",
        regs->si, regs->di, regs->bp, regs->sp);
    printf("  DS:  %04lx   ES:  %04lx   FS:  %04lx   GS:  %04lx\n",
        regs->ds, regs->es, regs->fs, regs->gs);
    printf("  EIP: %08lx  CS:  %04lx   EFLAGS: %08lx\n",
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
