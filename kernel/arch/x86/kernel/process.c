// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/lilac.h>
#include <lilac/process.h>
#include <lilac/libc.h>
#include <lilac/rwsem.h>
#include <lilac/sched.h>
#include <lilac/uaccess.h>
#include <lilac/wait.h>
#include <mm/mm.h>
#include <mm/kmm.h>
#include <mm/page.h>
#include <mm/tlb.h>
#include <asm/regs.h>
#include <asm/cpu.h>

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
    uintptr_t cr3 = virt_to_phys(get_zeroed_page());
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
    acquire_lock(&info->page_table_lock);
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
        kfree(desc);
        desc = next;
    }
    arch_tlb_flush_mmu(&tlb);
    info->mmap = NULL;
    release_lock(&info->page_table_lock);
    mmap_write_unlock(info);
}

void arch_reclaim_mem(struct task *p)
{
#ifdef DEBUG_MM
    mm_dbg_reclaim_pgd_pages_freed++;
#endif
    free_page(phys_to_virt(p->pgd));
}


#ifdef __x86_64__
static void copy_vm_area(void *cr3, struct vm_desc *new_desc)
{
    int num_pages = PAGE_ROUND_UP(new_desc->end - new_desc->start) / PAGE_SIZE;
    uintptr_t phys = virt_to_phys(get_zeroed_pages(num_pages, ALLOC_NORMAL));
#ifdef DEBUG_MM
    mm_dbg_fork_copy_pages_alloc += num_pages;
#endif

    if (!(new_desc->vm_flags & (VM_READ|VM_WRITE|VM_EXEC))) {
        for (int i = 0; i < num_pages; i++) {
            void *virt = (void*)(new_desc->start + i * PAGE_SIZE);
            uintptr_t src_phys = __walk_pages(virt);
            void *dst = phys_mem_mapping + phys + i * PAGE_SIZE;
            if (src_phys)
                memcpy(dst, phys_mem_mapping + src_phys, PAGE_SIZE);
            else
                memset(dst, 0, PAGE_SIZE);
        }
    } else {
        asm ("stac\n\t");
        memcpy((unsigned char*)phys_mem_mapping + phys, (void*)new_desc->start, num_pages * PAGE_SIZE);
        asm ("clac\n\t");
    }

    int flags = PG_USER;

    for (int i = 0; i < num_pages; i++) {
        void *virt = (void*)(new_desc->start + i * PAGE_SIZE);
        pml4e_t *pml4 = (pml4e_t*)cr3;
        pdpte_t *pdpt = get_or_alloc_pdpt(pml4, virt, flags | PG_WRITE | PG_PRESENT);
        pde_t *pd = get_or_alloc_pd(pdpt, virt, flags | PG_WRITE | PG_PRESENT);
        pte_t *pt = get_or_alloc_pt(pd, virt, flags | PG_WRITE | PG_PRESENT);

        if (!(new_desc->vm_flags & VM_EXEC))
            flags |= PG_EXEC_DISABLE;
        else
            flags |= PG_PRESENT;
        if (new_desc->vm_flags & VM_WRITE)
            flags |= PG_WRITE|PG_PRESENT;
        if (new_desc->vm_flags & VM_READ)
            flags |= PG_PRESENT;

        pt[get_pt_index(virt)] = (phys + i * PAGE_SIZE) | flags;
    }
}

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
    u64 *cr3 = (void*)(phys_mem_mapping + child->pgd);

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
        vma_list_insert(new_desc, &child->mmap);
        desc = desc->vm_next;

        copy_vm_area((void*)cr3, new_desc);
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

void *arch_copy_regs(struct regs_state *src)
{
    struct regs_state *regs = kzmalloc(sizeof *regs);
    if (!regs) {
        panic("Out of memory allocating regs_state\n");
    }
    *regs = *src;
    return regs;
}

void arch_set_user_sp(struct task *p, void *sp)
{
    struct regs_state *regs = (struct regs_state*)p->regs;
    if (!regs) {
        panic("Current task has no regs state\n");
    }
    regs->sp = (uintptr_t)sp;
}

void save_fp_regs(struct task *p)
{
    if (!p->fp_regs)
        p->fp_regs = kzmalloc(512);
    if (!p->fp_regs)
        panic("Out of memory allocating FP regs\n");
#ifdef __x86_64__
    __builtin_ia32_fxsave64(p->fp_regs);
#else
    __builtin_ia32_fxsave(p->fp_regs);
#endif
    // set TS flag
    // write_cr0(read_cr0() | X86_CR0_TS);
}

// TODO mem leaks
void copy_fp_regs(struct task *dst, struct task *src)
{
    dst->fp_regs = kmalloc(512);
    if (!src->fp_regs)
        save_fp_regs(src);
    if (!dst->fp_regs || !src->fp_regs)
        panic("FP regs not allocated for copy\n");
    memcpy(dst->fp_regs, src->fp_regs, 512);
}

void restore_fp_regs(struct task *p)
{
    if (p->fp_regs == NULL)
        return;
#ifdef __x86_64__
    __builtin_ia32_fxrstor64(p->fp_regs);
#else
    __builtin_ia32_fxrstor(p->fp_regs);
#endif
    // asm ("clts");
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

static void create_ucontext(ucontext_t *uc)
{
    uc->uc_link = NULL;
    uc->uc_sigmask = current->blocked;
    uc->uc_stack.ss_sp = NULL;
    uc->uc_stack.ss_flags = 0;
    uc->uc_stack.ss_size = 0;
}

void arch_prepare_signal(void *pc, int signo)
{
    struct regs_state *regs = (struct regs_state*)current->regs;
    klog(LOG_DEBUG, "Pre signal orginal regs: ip=%lx sp=%lx\n", regs->ip, regs->sp);
    uintptr_t *ustack = (uintptr_t*)regs->sp;
    uintptr_t return_addr;
    ucontext_t uc;

    // copy bytecode of sigtramp to user stack
    ustack = (uintptr_t*)((uintptr_t)ustack - 8);
    if (copy_to_user(ustack, sigtramp, 8))
        goto fail;

    return_addr = (uintptr_t)(ustack);

    // create ucontext struct
    ustack -= sizeof(ucontext_t) / sizeof(uintptr_t);
    create_ucontext(&uc);
    if (copy_to_user(ustack, &uc, sizeof(ucontext_t)))
        goto fail;

    // save registers onto user stack
    ustack -= sizeof(struct regs_state) / sizeof(uintptr_t);
    if (copy_to_user(ustack, regs, sizeof(struct regs_state)))
        goto fail;

    regs->ip = (uintptr_t)pc;
#ifdef __x86_64__
    regs->di = signo;
    regs->si = 0; // siginfo
    regs->dx = 0; // ucontext
#else
    *--ustack = 0; // ucontext
    *--ustack = 0; // siginfo
    *--ustack = (u32)signo; // First argument: signo
#endif
    // *--ustack = return_addr;
    if (put_user(return_addr, --ustack))
        goto fail;
    regs->sp = (uintptr_t)ustack;
    return;
fail:
    klog(LOG_WARN, "Failed to prepare signal frame for signal %d\n", signo);
    current->exit_status = WSIGNALED(SIGSEGV);
    do_exit();
}

// TODO: this state of registers must be validated
long arch_restore_post_signal(void)
{
    struct regs_state *regs = (struct regs_state*)current->regs;
    uintptr_t *stack = (uintptr_t*)regs->sp;
    ucontext_t *uc;

#ifndef __x86_64__
    // on 32 bit, pop the signal argument
    stack += 3; // skip args
#endif
    uc = (ucontext_t*)(stack + sizeof(struct regs_state) / sizeof(uintptr_t));
    if (get_user(current->blocked, &uc->uc_sigmask)) {
        klog(LOG_WARN, "Failed to get signal mask from user context in signal return, SIGSEGV raised\n");
        do_raise(current, SIGSEGV);
    }
    // Restore registers from user stack
    if (copy_from_user(regs, stack, sizeof(struct regs_state))) {
        klog(LOG_WARN, "Failed to copy regs from user stack in signal return, SIGSEGV raised\n");
        do_raise(current, SIGSEGV);
    }
    klog(LOG_DEBUG, "Post signal restored regs: ip=%lx sp=%lx\n", regs->ip, regs->sp);
    return regs->ax; // original return value
}
