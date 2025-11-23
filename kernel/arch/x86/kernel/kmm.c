// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <mm/kmm.h>
#include "paging.h"

uintptr_t __walk_pages(void *vaddr)
{
    return (uintptr_t)__get_physaddr(vaddr);
}

int x86_to_page_flags(int flags)
{
    int result = 0;
    if (flags & MEM_PF_WRITE)
        result |= PG_WRITE;
    if (flags & MEM_PF_USER)
        result |= PG_USER;
    if (flags & MEM_PF_NO_CACHE)
        result |= PG_CACHE_DISABLE;
    if (flags & MEM_PF_CACHE_WT)
        result |= PG_WRITE_THROUGH;
    if (flags & MEM_PF_GLOBAL)
        result |= PG_GLOBAL;
    if (flags & MEM_PF_UC)
        result |= (PG_CACHE_DISABLE | PG_WRITE_THROUGH);
    return result;
}

uintptr_t arch_get_pgd(void)
{
    uintptr_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}
