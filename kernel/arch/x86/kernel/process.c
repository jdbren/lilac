// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <string.h>
#include <lilac/process.h>
#include <lilac/types.h>
#include <lilac/panic.h>
#include <lilac/config.h>
#include <mm/kmm.h>
#include "pgframe.h"
#include "paging.h"

#define PG_DIR_INDEX(x) (((x) >> 22) & 0x3FF)
#define PG_TABLE_INDEX(x) (((x) >> 12) & 0x3FF)

extern const u32 _kernel_end;
static const int KERNEL_FIRST_PT = PG_DIR_INDEX(0xb0000000);

static u32 *pd = (u32*)0xFFFFF000;

struct mm_info arch_process_mmap()
{
    u32 *cr3 = map_phys(alloc_frame(), PAGE_BYTES, PG_WRITE);
    memset(cr3, 0, PAGE_SIZE);

    // Do recursive mapping
    cr3[1023] = virt_to_phys(cr3) | PG_WRITE | PG_SUPER | 1;

    // map io space
    // cr3[0] = pd[0];
    // printf("page_dir[0]: %x\n", cr3[0]);
    // printf("pd[0]: %x\n", pd[0]);

    // Allocate kernel stack
    void *kstack = kvirtual_alloc(__KERNEL_STACK_SZ, PG_WRITE);

    // Map kernel space
    for (int i = KERNEL_FIRST_PT; i < 1023; i++)
        cr3[i] = pd[i];

    struct mm_info info = {virt_to_phys(cr3), kstack};
    unmap_phys(cr3, PAGE_BYTES);

    return info;
}

void arch_user_stack()
{
    static const int num_pgs = __USER_STACK_SZ / PAGE_SIZE;
    void *stack = (void*)(__USER_STACK - __USER_STACK_SZ);
    map_pages(alloc_frames(num_pgs), stack, PG_USER | PG_WRITE, num_pgs);
}
