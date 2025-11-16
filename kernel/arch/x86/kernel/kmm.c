// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <mm/kmm.h>
#include "paging.h"

uintptr_t __walk_pages(void *vaddr)
{
    return (uintptr_t)get_physaddr(vaddr);
}

int x86_to_page_flags(int flags)
{
    int result = 0;
    if (flags & MEM_WRITE)
        result |= PG_WRITE;
    if (flags & MEM_USER)
        result |= PG_USER;
    if (flags & MEM_NO_CACHE)
        result |= PG_CACHE_DISABLE;
    if (flags & MEM_CACHE_WT)
        result |= PG_WRITE_THROUGH;
    if (flags & MEM_GLOBAL)
        result |= PG_GLOBAL;
    if (flags & MEM_UC)
        result |= (PG_CACHE_DISABLE | PG_WRITE_THROUGH);
    return result;
}
