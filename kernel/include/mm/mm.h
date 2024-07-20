#ifndef __MEMORY_MANAGEMENT_H_
#define __MEMORY_MANAGEMENT_H_

#include <lilac/types.h>
#include <lilac/sync.h>

#define PROT_NONE   0x00
#define PROT_READ	0x01
#define PROT_WRITE	0x02
#define PROT_EXEC	0x04

#define MAP_SHARED	    0x01
#define MAP_PRIVATE	    0x02
#define MAP_ANONYMOUS	0x04
#define MAP_FIXED	    0x08




struct vm_desc;

struct vm_desc {
    struct mm_struct *mm;
    uintptr_t start;
    uintptr_t end;

    /* list sorted by address */
    struct vm_desc *vm_next;
    // rb_node_t vm_rb;

    u32 vm_flags;

    u32 vm_pgoff;
    struct file *vm_file;
};


#endif
