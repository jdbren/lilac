// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/lilac.h>
#include <lilac/process.h>
#include <lilac/percpu.h>
#include <lilac/libc.h>
#include <lilac/rwsem.h>
#include <lilac/sched.h>
#include <lilac/uaccess.h>
#include <lilac/wait.h>
#include <lilac/fs.h>
#include <mm/mm.h>
#include <mm/kmm.h>
#include <mm/page.h>
#include <mm/tlb.h>
#include <asm/regs.h>
#include <asm/cpu.h>
#include <asm/segments.h>

#include "paging.h"

#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"

__maybe_unused
static void load_cr3(uintptr_t cr3)
{
    asm volatile ("mov %0, %%cr3" : : "r"(cr3));
}

#ifdef __i386__
__maybe_unused
static void print_page_structs(u32 *cr3)
{
    for (int i = 0; i < 1024; i++) {
        klog(LOG_DEBUG, "PD[%d]: %x\n", i, cr3[i]);
        if (cr3[i] & 1) {
            u32 *pt = map_phys((void*)(cr3[i] & ~0xFFF), PAGE_SIZE, MEM_PF_WRITE);
            for (int j = 0; j < 1024; j++) {
                if (pt[j] & 1) {
                    klog(LOG_DEBUG, "PT[%d]: %x\n", j, pt[j]);
                }
            }
            unmap_phys(pt, PAGE_SIZE);
        }
    }
}
#endif

#ifndef __x86_64__
static u32 *const pd = (u32*)0xFFFFF000UL;

static struct mm_info * make_32_bit_mmap()
{
    size_t *cr3 = get_zeroed_page();
    uintptr_t phys = virt_to_phys(cr3);
#ifdef DEBUG_MM
    mm_dbg_pgd_pages_alloc++;
#endif

    // Do recursive mapping
    cr3[1023] = phys | PG_WRITE | PG_SUPER | 1;

    // Map kernel space
    for (int i = PG_DIR_INDEX(0xc0000000); i < 1023; i++)
        cr3[i] = pd[i];

    struct mm_info *info = alloc_mm_info();
    if (!info) {
        kerror("Out of memory allocating mm_info\n");
    }
    info->pgd = phys;

    return info;
}
#endif

#ifdef __x86_64__
static struct mm_info * make_64_bit_mmap()
{
    void *pgd = get_zeroed_page();
    if (!pgd)
        panic("Out of memory allocating page directory\n");
    uintptr_t cr3 = virt_to_phys(pgd);
#ifdef DEBUG_MM
    mm_dbg_pgd_pages_alloc++;
#endif

    copy_kernel_mappings(cr3);

    struct mm_info *info = alloc_mm_info();
    if (!info) {
        free_page(phys_to_virt(cr3));
        panic("Out of memory allocating mm_info\n");
    }
    info->pgd = cr3;

    return info;
}
#endif

struct mm_info * arch_process_mmap(__maybe_unused bool is_64_bit)
{
#ifdef __x86_64__
    return make_64_bit_mmap();
#else
    return make_32_bit_mmap();
#endif
}

void arch_unmap_all_user_vm(struct mm_info *info)
{
    struct tlb_inval tlb = {
        .mm = info,
        .start = 0,
        .end = __USER_MAX_ADDR + 1,
        .full = true,
    };

    klog(LOG_DEBUG, "Unmapping all user VM for mm %p\n", info);
    mmap_write_lock(info);
    // writeback_vma_range takes the page table lock itself
    for (struct vm_desc *d = info->mmap; d; d = d->vm_next)
        if (!(d->vm_flags & VM_IO))
            writeback_vma_range(d, d->start, d->end);
    lock_page_table(info);
    struct vm_desc *desc = info->mmap;
    while (desc) {
        struct vm_desc *next = desc->vm_next;
    #ifdef DEBUG_MM
        mm_dbg_unmap_requested_pages += (desc->end - desc->start) / PAGE_SIZE;
    #endif
#ifdef DEBUG_MM
        klog(LOG_DEBUG, "Unmapping %x-%x\n", desc->start, desc->end);
#endif
        if (desc->vm_flags & VM_IO) {
            unmap_pages((void*)desc->start, (desc->end - desc->start) / PAGE_SIZE);
        } else {
            drop_user_page_range(desc->start, desc->end - desc->start);
        }
        if (desc->vm_file)
            fput(desc->vm_file);
        kfree(desc);
        desc = next;
    }
    tlb_shootdown(&tlb);
    info->mmap = NULL;
    unlock_page_table(info);
    mmap_write_unlock(info);
}

void arch_free_old_pgd(struct mm_info *old)
{
    load_cr3(current->pgd);
#ifdef DEBUG_MM
    mm_dbg_reclaim_pgd_pages_freed++;
#endif
    free_page(phys_to_virt(old->pgd));
}

void arch_reclaim_mem(struct task *p)
{
#ifdef DEBUG_MM
    mm_dbg_reclaim_pgd_pages_freed++;
#endif
    free_page(phys_to_virt(p->pgd));
}


#ifdef __x86_64__
struct mm_info *arch_copy_mmap(struct mm_info *parent)
{
    struct mm_info *child = arch_process_mmap(sizeof(void*) == 8);
    child->start_code = parent->start_code;
    child->end_code = parent->end_code;
    child->start_data = parent->start_data;
    child->end_data = parent->end_data;
    child->start_brk = parent->start_brk;
    child->brk = parent->brk;
    child->start_stack = parent->start_stack;
    child->total_vm = parent->total_vm;

    struct vm_desc *desc = parent->mmap;
    while (desc) {
        struct vm_desc *new_desc = kzmalloc(sizeof *new_desc);
        if (!new_desc) {
            panic("Out of memory allocating vm_desc for fork\n");
        }
#ifdef DEBUG_FORK
        klog(LOG_DEBUG, "Copying VMA %lx-%lx\n", desc->start, desc->end);
#endif
        *new_desc = *desc;
        new_desc->mm = child;
        new_desc->vm_next = NULL;
        new_desc->vm_prev = NULL;
        if (new_desc->vm_file)
            fget(new_desc->vm_file);
        vma_list_insert(new_desc, &child->mmap);
        desc = desc->vm_next;

        fork_copy_vm_area(child->pgd, parent, new_desc->start, new_desc->end,
            new_desc->vm_flags);
    }

    return child;
}
#else
struct mm_info *arch_copy_mmap(struct mm_info *parent)
{
    struct mm_info *child = arch_process_mmap(sizeof(void*) == 8);
    child->start_code = parent->start_code;
    child->end_code = parent->end_code;
    child->start_data = parent->start_data;
    child->end_data = parent->end_data;
    child->brk = parent->brk;
    child->start_stack = parent->start_stack;
    child->total_vm = parent->total_vm;
    u32 *cr3 = phys_to_virt(child->pgd);

    struct vm_desc *desc = parent->mmap;
    while (desc) {
        struct vm_desc *new_desc = kzmalloc(sizeof *new_desc);
        if (!new_desc) {
            panic("Out of memory allocating vm_desc for fork\n");
        }

        *new_desc = *desc;
        new_desc->mm = child;
        new_desc->vm_next = NULL;
        new_desc->vm_prev = NULL;
        if (new_desc->vm_file)
            fget(new_desc->vm_file);
        vma_list_insert(new_desc, &child->mmap);
        desc = desc->vm_next;

        int num_pages = PAGE_ROUND_UP(new_desc->end - new_desc->start) / PAGE_SIZE;
        uintptr_t phys = virt_to_phys(get_zeroed_pages(num_pages, ALLOC_NORMAL));
    #ifdef DEBUG_MM
        mm_dbg_fork_copy_pages_alloc += num_pages;
    #endif

        // Copy data
        memcpy(phys_to_virt(phys), (void*)new_desc->start, num_pages * PAGE_SIZE);

        for (int i = 0; i < num_pages; i++) {
            u32 pdindex = PG_DIR_INDEX(new_desc->start + i * PAGE_SIZE);
            u32 ptindex = PG_TABLE_INDEX(new_desc->start + i * PAGE_SIZE);

            if (!(cr3[pdindex] & 1)) {
                u32 *pt = get_zeroed_page();
                cr3[pdindex] = virt_to_phys(pt) | PG_WRITE | PG_USER | 1;
            }

            u32 *pt = phys_to_virt(cr3[pdindex] & ~0xFFF);
            pt[ptindex] = (phys + i * PAGE_SIZE) | PG_WRITE | PG_USER | 1;
        }
    }

    return child;
}
#endif

int arch_do_fork(void)
{
    return 0;
}

void *arch_get_user_sp(void)
{
    struct regs_state *regs = (struct regs_state*)current->regs;
    if (!regs) {
        panic("Current task has no regs state\n");
    }
    return (void*)regs->sp;
}

struct regs_state * alloc_regs_state(void)
{
    return kzmalloc(sizeof(struct regs_state));
}

void *arch_copy_regs(struct regs_state *dst, struct regs_state *src)
{
    *dst = *src;
    return dst;
}

void arch_set_user_sp(struct task *p, void *sp)
{
    struct regs_state *regs = (struct regs_state*)p->regs;
    if (!regs) {
        panic("Current task has no regs state\n");
    }
    regs->sp = (uintptr_t)sp;
}

__section(".sigtramp") __noreturn
void sigtramp(void)
{
#ifdef __x86_64__
    asm volatile ("syscall": : "a"(31));
#else
    asm volatile ("int $0x80": : "a"(31));
#endif
    unreachable();
}

#ifndef __x86_64__
/*
 * Signal frame, from regs_frame (16-byte aligned) up:
 * [regs_state][ucontext][pad][fxsave area] ... [siginfo][sigtramp bytes]
 */
#define SIGFRAME_FP_OFFSET  ((sizeof(struct regs_state) + sizeof(ucontext_t) + 15) & ~15UL)
#define SIGFRAME_FP_BYTES   512

static void create_ucontext(ucontext_t *uc)
{
    uc->uc_link = NULL;
    // sigreturn restores this; after sigsuspend it must be the pre-suspend mask
    if (current->flags.restore_sigmask) {
        uc->uc_sigmask = current->saved_sigmask;
        current->flags.restore_sigmask = 0;
    } else {
        uc->uc_sigmask = current->blocked;
    }
    uc->uc_stack.ss_sp = NULL;
    uc->uc_stack.ss_flags = 0;
    uc->uc_stack.ss_size = 0;
}

void arch_prepare_signal(void *pc, int signo, const struct ksiginfo *info,
                         void *restorer)
{
    static const size_t FRAME_BYTES = SIGFRAME_FP_OFFSET + SIGFRAME_FP_BYTES;
    struct regs_state *regs = (struct regs_state*)current->regs;
#ifdef DEBUG_SIGNAL
    klog(LOG_DEBUG, "Pre signal orginal regs: ip=%lx sp=%lx\n", regs->ip, regs->sp);
#endif
    uintptr_t *ustack = (uintptr_t*)regs->sp;
    uintptr_t return_addr;
    ucontext_t uc;

    ustack = (uintptr_t*)((uintptr_t)ustack - 8);
    if (restorer) {
        return_addr = (uintptr_t)restorer;
    } else {
        // copy bytecode of sigtramp to user stack
        if (copy_to_user(ustack, sigtramp, 8))
            goto fail;
        return_addr = (uintptr_t)(ustack);
    }

    /*
     * 16 byte alignment
     * [return addr][regs_state][ucontext][sigtramp bytes]
     */
    const uintptr_t sigtramp_addr = (uintptr_t)ustack;
    // [return addr][regs_state][ucontext] ... [siginfo][sigtramp bytes]
    const uintptr_t info_frame = (sigtramp_addr - sizeof(struct user_siginfo)) & ~0xFUL;
    const uintptr_t regs_frame = (info_frame - FRAME_BYTES) & ~0xFUL;
    const uintptr_t uc_frame = regs_frame + sizeof(struct regs_state);

    if (info) {
        struct user_siginfo si;
        memset(&si, 0, sizeof(si));
        si.si_signo = signo;
        si.si_code = info->code;
        if (signo == SIGSEGV || signo == SIGBUS || signo == SIGILL || signo == SIGFPE) {
            si.addr = info->addr;
        } else {
            si.kill.pid = info->pid;
            si.kill.status = info->status;
        }
        if (copy_to_user((void*)info_frame, &si, sizeof(si)))
            goto fail;
    }

    // create ucontext struct
    create_ucontext(&uc);
    if (copy_to_user((void*)uc_frame, &uc, sizeof(ucontext_t)))
        goto fail;

    // save registers onto user stack
    if (copy_to_user((void*)regs_frame, regs, sizeof(struct regs_state)))
        goto fail;

    if (copy_to_user((void*)(regs_frame + SIGFRAME_FP_OFFSET),
            fpu_sigframe_state(current), SIGFRAME_FP_BYTES))
        goto fail;
    fpu_sigframe_reset(current);

    regs->ip = (uintptr_t)pc;
    ustack = (uintptr_t*)regs_frame;
    *--ustack = 0; // ucontext
    *--ustack = 0; // siginfo
    *--ustack = (u32)signo; // First argument: signo
    // x86 32-bit cdecl entry frame: [return][arg1][arg2][arg3]
    if (put_user(return_addr, --ustack))
        goto fail;
    assert(((uintptr_t)ustack & 0xFUL) == 0);
    regs->sp = (uintptr_t)ustack;
    return;
fail:
    klog(LOG_WARN, "Failed to prepare signal frame for signal %d\n", signo);
    current->exit_status = WSIGNALED(SIGSEGV);
    do_exit();
}
#endif /* !__x86_64__ */

static bool validate_regs_state(struct regs_state *regs)
{
    if (!regs) {
        klog(LOG_WARN, "Regs state is NULL\n");
        return false;
    }
    if (regs->cs != __USER_CS || regs->ss != __USER_DS) {
        klog(LOG_WARN, "Regs state has invalid segment selectors: cs=%lx ss=%lx\n",
            regs->cs, regs->ss);
        return false;
    }
    if (regs->sp > __USER_MAX_ADDR) {
        klog(LOG_WARN, "Regs state has invalid stack pointer: sp=%lx\n", regs->sp);
        return false;
    }
    if (regs->ip > __USER_MAX_ADDR) {
        klog(LOG_WARN, "Regs state has invalid instruction pointer: ip=%lx\n", regs->ip);
        return false;
    }
    return true;
}

#ifndef __x86_64__
long arch_restore_post_signal(void)
{
    struct regs_state *regs = (struct regs_state*)current->regs;
    uintptr_t *stack = (uintptr_t*)regs->sp;
    ucontext_t *uc;
    void *fpstate;
    sigset_t mask;

    // pop the signal arguments
    stack += 3;
    // Not a restartable syscall
    current->syscall_nr = -1;
    uc = (ucontext_t*)(stack + sizeof(struct regs_state) / sizeof(uintptr_t));
    if (get_user(mask, &uc->uc_sigmask)) {
        klog(LOG_WARN, "Failed to get signal mask from user context in signal return, SIGSEGV raised\n");
        do_kill(current, SIGSEGV);
    }
    current->blocked = SIG_APPLY_MASK(mask, _SIGKILL | _SIGSTOP);

    // Restore registers from user stack
    if (copy_from_user(regs, stack, sizeof(struct regs_state))) {
        klog(LOG_WARN, "Failed to copy regs from user stack in signal return, SIGSEGV raised\n");
        do_kill(current, SIGSEGV);
    }

    if (!validate_regs_state(regs)) {
        klog(LOG_WARN, "Invalid regs state in signal return, SIGSEGV raised\n");
        do_kill(current, SIGSEGV);
    }

    fpstate = fpu_sigframe_restore_buf(current);
    if (!fpstate || copy_from_user(fpstate, (u8*)stack + SIGFRAME_FP_OFFSET, SIGFRAME_FP_BYTES)) {
        klog(LOG_WARN, "Failed to restore FP state in signal return, SIGSEGV raised\n");
        do_kill(current, SIGSEGV);
    } else {
        fpu_sigframe_restore_fixup(current);
    }

#ifdef DEBUG_SIGNAL
    klog(LOG_DEBUG, "Post signal restored regs: ip=%lx sp=%lx\n", regs->ip, regs->sp);
#endif
    return regs->ax; // original return value
}
#endif /* !__x86_64__ */

#ifdef __x86_64__
/*
 * Linux x86_64 rt_sigframe layout, which musl's ucontext_t/mcontext_t expect.
 * From the handler's sp (sp % 16 == 8, as after a call) up:
 * [pretcode][ucontext][siginfo] ... [fxsave area, 64-aligned][sigtramp bytes]
 */
enum {
    REG_R8, REG_R9, REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15,
    REG_RDI, REG_RSI, REG_RBP, REG_RBX, REG_RDX, REG_RAX, REG_RCX, REG_RSP,
    REG_RIP, REG_EFL, REG_CSGSFS, REG_ERR, REG_TRAPNO, REG_OLDMASK, REG_CR2,
    NGREG
};

struct sig_mcontext {
    unsigned long gregs[NGREG];
    void *fpregs;               /* -> fxsave area */
    unsigned long __reserved1[8];
};

struct sig_ucontext {
    unsigned long uc_flags;
    struct sig_ucontext *uc_link;
    stack_t uc_stack;
    struct sig_mcontext uc_mcontext;
    sigset_t uc_sigmask;
};

struct rt_sigframe {
    unsigned long pretcode;
    struct sig_ucontext uc;
    struct user_siginfo info;
};

#define SIGFRAME_FP_BYTES   512
// flags a signal handler may change through uc_mcontext
#define SIGFRAME_USER_FLAGS 0x50dd5UL /* AC RF OF DF TF SF ZF AF PF CF */

static void regs_to_gregs(unsigned long *g, const struct regs_state *r)
{
    g[REG_R8] = r->r8;   g[REG_R9] = r->r9;   g[REG_R10] = r->r10;
    g[REG_R11] = r->r11; g[REG_R12] = r->r12; g[REG_R13] = r->r13;
    g[REG_R14] = r->r14; g[REG_R15] = r->r15; g[REG_RDI] = r->di;
    g[REG_RSI] = r->si;  g[REG_RBP] = r->bp;  g[REG_RBX] = r->bx;
    g[REG_RDX] = r->dx;  g[REG_RAX] = r->ax;  g[REG_RCX] = r->cx;
    g[REG_RSP] = r->sp;  g[REG_RIP] = r->ip;  g[REG_EFL] = r->flags;
    g[REG_CSGSFS] = r->cs;
}

static void gregs_to_regs(struct regs_state *r, const unsigned long *g)
{
    r->r8 = g[REG_R8];   r->r9 = g[REG_R9];   r->r10 = g[REG_R10];
    r->r11 = g[REG_R11]; r->r12 = g[REG_R12]; r->r13 = g[REG_R13];
    r->r14 = g[REG_R14]; r->r15 = g[REG_R15]; r->di = g[REG_RDI];
    r->si = g[REG_RSI];  r->bp = g[REG_RBP];  r->bx = g[REG_RBX];
    r->dx = g[REG_RDX];  r->ax = g[REG_RAX];  r->cx = g[REG_RCX];
    r->sp = g[REG_RSP];  r->ip = g[REG_RIP];
    r->flags = (r->flags & ~SIGFRAME_USER_FLAGS) | (g[REG_EFL] & SIGFRAME_USER_FLAGS);
    r->cs = __USER_CS;
    r->ss = __USER_DS;
}

void arch_prepare_signal(void *pc, int signo, const struct ksiginfo *info,
                         void *restorer)
{
    struct regs_state *regs = (struct regs_state*)current->regs;
#ifdef DEBUG_SIGNAL
    klog(LOG_DEBUG, "Pre signal orginal regs: ip=%lx sp=%lx\n", regs->ip, regs->sp);
#endif
    // The interrupted code may have live data in its 128-byte red zone
    uintptr_t top = regs->sp - 128;
    uintptr_t return_addr;
    struct rt_sigframe frame;

    if (restorer) {
        return_addr = (uintptr_t)restorer;
    } else {
        // copy bytecode of sigtramp to user stack
        top -= 8;
        if (copy_to_user((void*)top, sigtramp, 8))
            goto fail;
        return_addr = top;
    }

    const uintptr_t fp_addr = (top - SIGFRAME_FP_BYTES) & ~63UL;
    const uintptr_t frame_addr = ((fp_addr - sizeof(frame)) & ~15UL) - 8;
    struct rt_sigframe *uframe = (struct rt_sigframe*)frame_addr;

    memset(&frame, 0, sizeof(frame));
    frame.pretcode = return_addr;
    frame.info.si_signo = signo;
    if (info) {
        frame.info.si_code = info->code;
        if (signo == SIGSEGV || signo == SIGBUS || signo == SIGILL || signo == SIGFPE) {
            frame.info.addr = info->addr;
            frame.uc.uc_mcontext.gregs[REG_CR2] = info->addr;
        } else {
            frame.info.kill.pid = info->pid;
            frame.info.kill.status = info->status;
        }
    }
    regs_to_gregs(frame.uc.uc_mcontext.gregs, regs);
    frame.uc.uc_mcontext.fpregs = (void*)fp_addr;
    // sigreturn restores this; after sigsuspend it must be the pre-suspend mask
    if (current->flags.restore_sigmask) {
        frame.uc.uc_sigmask = current->saved_sigmask;
        current->flags.restore_sigmask = 0;
    } else {
        frame.uc.uc_sigmask = current->blocked;
    }

    if (copy_to_user(uframe, &frame, sizeof(frame)))
        goto fail;
    if (copy_to_user((void*)fp_addr, fpu_sigframe_state(current), SIGFRAME_FP_BYTES))
        goto fail;
    fpu_sigframe_reset(current);

    regs->ip = (uintptr_t)pc;
    regs->di = signo;
    regs->si = (uintptr_t)&uframe->info;
    regs->dx = (uintptr_t)&uframe->uc;
    regs->ax = 0;
    regs->sp = frame_addr;
    assert((frame_addr & 0xFUL) == 8);
    return;
fail:
    klog(LOG_WARN, "Failed to prepare signal frame for signal %d\n", signo);
    current->exit_status = WSIGNALED(SIGSEGV);
    do_exit();
}

long arch_restore_post_signal(void)
{
    struct regs_state *regs = (struct regs_state*)current->regs;
    // the handler's ret popped pretcode, so sp is at the ucontext
    struct sig_ucontext *uuc = (struct sig_ucontext*)regs->sp;
    struct sig_ucontext uc;
    void *fpstate;

    current->syscall_nr = -1;
    if (copy_from_user(&uc, uuc, sizeof(uc))) {
        klog(LOG_WARN, "Failed to read user context in signal return, SIGSEGV raised\n");
        do_kill(current, SIGSEGV);
    }
    // the frame is user memory: it can't block what sigprocmask can't
    current->blocked = SIG_APPLY_MASK(uc.uc_sigmask, _SIGKILL | _SIGSTOP);
    gregs_to_regs(regs, uc.uc_mcontext.gregs);

    if (!validate_regs_state(regs)) {
        klog(LOG_WARN, "Invalid regs state in signal return, SIGSEGV raised\n");
        do_kill(current, SIGSEGV);
    }

    if (!uc.uc_mcontext.fpregs) {
        fpu_sigframe_reset(current);
        goto out;
    }
    fpstate = fpu_sigframe_restore_buf(current);
    if (!fpstate || copy_from_user(fpstate, uc.uc_mcontext.fpregs, SIGFRAME_FP_BYTES)) {
        klog(LOG_WARN, "Failed to restore FP state in signal return, SIGSEGV raised\n");
        do_kill(current, SIGSEGV);
    } else {
        fpu_sigframe_restore_fixup(current);
    }

out:
#ifdef DEBUG_SIGNAL
    klog(LOG_DEBUG, "Post signal restored regs: ip=%lx sp=%lx\n", regs->ip, regs->sp);
#endif
    return regs->ax; // original return value
}
#endif /* __x86_64__ */
