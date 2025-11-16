// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include "paging.h"

uintptr_t __walk_pages(void *vaddr)
{
    return (uintptr_t)get_physaddr(vaddr);
}
