// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/process.h>
#include <lilac/libc.h>
#include <lilac/sched.h>
#include <lilac/types.h>
#include <lilac/panic.h>
#include <lilac/config.h>
#include <lilac/mm.h>
#include <lilac/kmm.h>
#include <lilac/kmalloc.h>
#include "pgframe.h"
#include "paging.h"
#include <asm/regs.h>

#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"

extern const u32 _kernel_end;

static u32 *const pd = (u32*)0xFFFFF000UL;

static void load_cr3(uintptr_t cr3)
{
    asm volatile ("mov %0, %%cr3" : : "r"(cr3));
}

#ifndef __x86_64__
static void print_page_structs(u32 *cr3)
{
    for (int i = 0; i < 1024; i++) {
        klog(LOG_DEBUG, "PD[%d]: %x\n", i, cr3[i]);
        if (cr3[i] & 1) {
            u32 *pt = map_phys((void*)(cr3[i] & ~0xFFF), PAGE_BYTES, PG_WRITE);
            for (int j = 0; j < 1024; j++) {
                if (pt[j] & 1) {
                    klog(LOG_DEBUG, "PT[%d]: %x\n", j, pt[j]);
                }
            }
            unmap_phys(pt, PAGE_BYTES);
        }
    }
}
#endif

#ifndef __x86_64__
static struct mm_info * make_32_bit_mmap()
{
    volatile uintptr_t *cr3 = map_phys(alloc_frame(), PAGE_BYTES, PG_WRITE);
    memset((void*)cr3, 0, PAGE_SIZE);

    // Do recursive mapping
    cr3[1023] = virt_to_phys((void*)cr3) | PG_WRITE | PG_SUPER | 1;

    // Allocate kernel stack
    void *kstack = kvirtual_alloc(__KERNEL_STACK_SZ, PG_WRITE);

    // Map kernel space
    for (int i = PG_DIR_INDEX(0xc0000000); i < 1023; i++)
        cr3[i] = pd[i];

    struct mm_info *info = kzmalloc(sizeof *info);
    if (!info) {
        kerror("Out of memory allocating mm_info\n");
    }
    info->pgd = virt_to_phys((void*)cr3);
    info->kstack = kstack;
    cr3[1023] = virt_to_phys((void*)cr3) | PG_WRITE | PG_SUPER | 1;
    unmap_phys((void*)cr3, PAGE_BYTES);

    return info;
}
#endif

#ifdef __x86_64__
static struct mm_info * make_64_bit_mmap()
{
    uintptr_t cr3 = (uintptr_t)alloc_frame();

    copy_kernel_mappings(cr3);

    struct mm_info *info = kzmalloc(sizeof *info);
    if (!info) {
        free_frame((void*)cr3);
        kerror("Out of memory allocating mm_info\n");
    }
    info->pgd = cr3;
    info->kstack = kvirtual_alloc(__KERNEL_STACK_SZ, PG_WRITE);

    return info;
}
#else
static struct mm_info * make_64_bit_mmap() {return NULL;}
#endif

struct mm_info * arch_process_mmap(bool is_64_bit)
{
#ifdef __x86_64__
    return make_64_bit_mmap();
#else
    return make_32_bit_mmap();
#endif
}

void arch_unmap_all_user_vm(struct mm_info *info)
{
    struct vm_desc *desc = info->mmap;
    while (desc) {
        struct vm_desc *next = desc->vm_next;
        klog(LOG_DEBUG, "Unmapping %x-%x\n", desc->start, desc->end);
        for (uintptr_t addr = desc->start; addr < desc->end; addr += PAGE_SIZE) {
            uintptr_t phys = virt_to_phys((void*)addr);
            unmap_page((void*)addr);
            free_frame((void*)phys);
        }
        kfree(desc);
        desc = next;
    }
    info->mmap = NULL;
}

void arch_reclaim_mem(struct task *p)
{
    free_frame((void*)(p->pgd));
}

struct mm_info * arch_process_remap(struct mm_info *existing)
{
    arch_unmap_all_user_vm(existing);
    return existing;
}

void * arch_user_stack(void)
{
    static const int num_pgs = __USER_STACK_SZ / PAGE_SIZE;
    void *stack = (void*)(__USER_STACK - __USER_STACK_SZ);
    map_pages(alloc_frames(num_pgs), stack, PG_USER | PG_WRITE, num_pgs);
    return stack;
}

#ifdef __x86_64__
static void copy_vm_area(void *cr3, struct vm_desc *new_desc)
{
    int num_pages = PAGE_ROUND_UP(new_desc->end - new_desc->start) / PAGE_SIZE;
    void *phys = alloc_frames(num_pages);

    // Copy data
    memcpy((unsigned char*)phys_mem_mapping + (uintptr_t)phys, (void*)new_desc->start, num_pages * PAGE_SIZE);

    int flags = PG_USER | PG_PRESENT;

    for (int i = 0; i < num_pages; i++) {
        void *virt = (void*)(new_desc->start + i * PAGE_SIZE);
        pml4e_t *pml4 = (pml4e_t*)cr3;
        pdpte_t *pdpt = get_or_alloc_pdpt(pml4, virt, flags | PG_WRITE);
        pde_t *pd = get_or_alloc_pd(pdpt, virt, flags | PG_WRITE);
        pte_t *pt = get_or_alloc_pt(pd, virt, flags | PG_WRITE);

        if (new_desc->vm_prot & PROT_WRITE)
            flags |= PG_WRITE;

        pt[get_pt_index(virt)] = ((uintptr_t)phys + i * PAGE_SIZE) | flags;
    }
}

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
    u64 *cr3 = (void*)(phys_mem_mapping + child->pgd);

    struct vm_desc *desc = parent->mmap;
    while (desc) {
        struct vm_desc *new_desc = kzmalloc(sizeof *new_desc);
        if (!new_desc) {
            kerror("Out of memory allocating vm_desc for fork\n");
        }
#ifdef DEBUG_FORK
        klog(LOG_DEBUG, "Copying VMA %lx-%lx\n", desc->start, desc->end);
#endif
        *new_desc = *desc;
        new_desc->mm = child;
        new_desc->vm_next = NULL;
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
    u32 *cr3 = map_phys((void*)child->pgd, PAGE_BYTES, PG_WRITE);

    struct vm_desc *desc = parent->mmap;
    while (desc) {
        struct vm_desc *new_desc = kzmalloc(sizeof *new_desc);
        if (!new_desc) {
            kerror("Out of memory allocating vm_desc for fork\n");
        }
        memcpy(new_desc, desc, sizeof *desc);
        new_desc->mm = child;
        new_desc->vm_next = NULL;
        vma_list_insert(new_desc, &child->mmap);
        desc = desc->vm_next;

        int num_pages = PAGE_ROUND_UP(new_desc->end - new_desc->start) / PAGE_SIZE;
        void *phys = alloc_frames(num_pages);

        // Copy data
        void *tmp_virt = map_phys(phys, num_pages * PAGE_SIZE, PG_WRITE);
        memcpy(tmp_virt, (void*)new_desc->start, num_pages * PAGE_SIZE);
        unmap_phys(tmp_virt, num_pages * PAGE_SIZE);

        for (int i = 0; i < num_pages; i++) {
            u32 pdindex = PG_DIR_INDEX(new_desc->start + i * PAGE_SIZE);
            u32 ptindex = PG_TABLE_INDEX(new_desc->start + i * PAGE_SIZE);

            if (!(cr3[pdindex] & 1)) {
                u32 *pt = map_phys(alloc_frame(), PAGE_BYTES, PG_WRITE);
                memset(pt, 0, PAGE_SIZE);
                cr3[pdindex] = virt_to_phys(pt) | PG_WRITE | PG_USER | 1;
                unmap_phys(pt, PAGE_BYTES);
            }

            u32 *pt = map_phys((void*)(cr3[pdindex] & ~0xFFF), PAGE_BYTES, PG_WRITE);
            pt[ptindex] = ((uintptr_t)phys + i * PAGE_SIZE) | PG_WRITE | PG_USER | 1;
            unmap_phys(pt, PAGE_BYTES);
        }
    }

    unmap_phys(cr3, PAGE_BYTES);

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
        kerror("Current task has no regs state\n");
    }
    return (void*)regs->sp;
}

void *arch_copy_regs(struct regs_state *src)
{
    struct regs_state *regs = kzmalloc(sizeof *regs);
    if (!regs) {
        kerror("Out of memory allocating regs_state\n");
    }
    *regs = *src;
    return regs;
}

void save_fp_regs(struct task *p)
{
    if (!p->fp_regs)
        p->fp_regs = kzmalloc(512);
    if (!p->fp_regs)
        kerror("Out of memory allocating FP regs\n");
    asm volatile("fxsave %0" : : "m" (*(char (*)[512])p->fp_regs) : "memory");
}

void copy_fp_regs(struct task *dst, struct task *src)
{
    dst->fp_regs = kmalloc(512);
    if (!dst->fp_regs || !src->fp_regs)
        kerror("FP regs not allocated for copy\n");
    memcpy(dst->fp_regs, src->fp_regs, 512);
}

void restore_fp_regs(struct task *p)
{
    if (p->fp_regs == NULL)
        return;
    asm volatile("fxrstor %0" : : "m" (*(const char (*)[512])p->fp_regs) : "memory");
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

    // copy bytecode of sigtramp to user stack
    ustack = (uintptr_t*)((uintptr_t)ustack - 8);
    memcpy(ustack, sigtramp, 8);
    return_addr = (uintptr_t)(ustack);
    // create ucontext struct
    ustack -= sizeof(ucontext_t) / sizeof(uintptr_t);
    create_ucontext((ucontext_t*)ustack);
    // save registers onto user stack
    ustack -= sizeof(struct regs_state) / sizeof(uintptr_t);
    memcpy(ustack, regs, sizeof(struct regs_state));

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
    *--ustack = return_addr;
    regs->sp = (uintptr_t)ustack;
}

void arch_restore_post_signal(void)
{
    struct regs_state *regs = (struct regs_state*)current->regs;
    uintptr_t *stack = (uintptr_t*)regs->sp;
    ucontext_t *uc;

#ifndef __x86_64__
    // on 32 bit, pop the signal argument
    stack += 3; // skip args
#endif
    uc = (ucontext_t*)(stack + sizeof(struct regs_state) / sizeof(uintptr_t));
    current->blocked = uc->uc_sigmask;
    // Restore registers from user stack
    memcpy(regs, stack, sizeof(struct regs_state));
    klog(LOG_DEBUG, "Post signal restored regs: ip=%lx sp=%lx\n", regs->ip, regs->sp);
}
