// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/process.h>
#include <lilac/libc.h>
#include <lilac/sched.h>
#include <lilac/types.h>
#include <lilac/panic.h>
#include <lilac/config.h>
#include <lilac/mm.h>
#include <mm/kmm.h>
#include <mm/kmalloc.h>
#include "pgframe.h"
#include "paging.h"
#include <asm/regs.h>

extern const u32 _kernel_end;

static u32 *const pd = (u32*)0xFFFFF000UL;

static void load_cr3(uintptr_t cr3)
{
    asm volatile ("mov %0, %%cr3" : : "r"(cr3));
}

#ifndef ARCH_x86_64
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

#ifndef ARCH_x86_64
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
    info->pgd = virt_to_phys((void*)cr3);
    info->kstack = kstack;
    cr3[1023] = virt_to_phys((void*)cr3) | PG_WRITE | PG_SUPER | 1;
    unmap_phys((void*)cr3, PAGE_BYTES);

    return info;
}
#endif

#ifdef ARCH_x86_64
static struct mm_info * make_64_bit_mmap()
{
    uintptr_t cr3 = (uintptr_t)alloc_frame();

    copy_kernel_mappings(cr3);

    struct mm_info *info = kzmalloc(sizeof *info);
    info->pgd = cr3;
    info->kstack = kvirtual_alloc(__KERNEL_STACK_SZ, PG_WRITE);

    return info;
}
#else
static struct mm_info * make_64_bit_mmap() {return NULL;}
#endif

struct mm_info * arch_process_mmap(bool is_64_bit)
{
#ifdef ARCH_x86_64
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
        uintptr_t phys = virt_to_phys((void*)desc->start);
        unmap_pages((void*)desc->start, (desc->end - desc->start) / PAGE_SIZE);
        free_frames((void*)phys, (desc->end - desc->start) / PAGE_SIZE);
        kfree(desc);
        desc = next;
    }
    info->mmap = NULL;
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

#ifdef ARCH_x86_64
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
        memcpy(new_desc, desc, sizeof *desc);
        new_desc->mm = child;
        new_desc->vm_next = NULL;
        vma_list_insert(new_desc, &child->mmap);
        desc = desc->vm_next;

        int num_pages = PAGE_ROUND_UP(new_desc->end - new_desc->start) / PAGE_SIZE;
        void *phys = alloc_frames(num_pages);

        // Copy data
        void *tmp_virt = phys_mem_mapping + (uintptr_t)phys;
        memcpy(tmp_virt, (void*)new_desc->start, num_pages * PAGE_SIZE);

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

int arch_do_fork(struct regs_state *regs)
{
    klog(LOG_DEBUG, "Forking process\n");
    klog(LOG_DEBUG, "\tIP: %x\n", regs->ip);
    klog(LOG_DEBUG, "\tSP: %x\n", regs->sp);
    current->regs = (void*)regs;
    return do_fork();
}

void *arch_copy_regs(struct regs_state *src)
{
    struct regs_state *regs = kzmalloc(sizeof *regs);
    *regs = *src;
    return regs;
}
