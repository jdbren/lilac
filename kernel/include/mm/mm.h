#ifndef _LILAC_MM_H_
#define _LILAC_MM_H_

#include <lilac/types.h>
#include <lilac/sync.h>

#define PROT_NONE   0x00
#define PROT_READ	0x04
#define PROT_WRITE	0x02
#define PROT_EXEC	0x01

#define MAP_SHARED	    0x01
#define MAP_PRIVATE	    0x02
#define MAP_ANONYMOUS	0x04
#define MAP_FIXED	    0x08

#define VM_TEXT     1
#define VM_RODATA   2
#define VM_DATA     3
#define VM_BSS      4
#define VM_STACK    5

struct vm_desc;

struct vm_desc {
    uintptr_t start;
    uintptr_t end;
    struct mm_info *mm;

    /* list sorted by address */
    struct vm_desc *vm_next;
    // rb_node_t vm_rb;

    u32 vm_prot;
    u32 vm_flags;

    u32 vm_pgoff;
    struct file *vm_file;
};

int umem_alloc(uintptr_t vaddr, int num_pages);
void vma_list_insert(struct vm_desc *vma, struct vm_desc **list);

void * sbrk(intptr_t increment);

#endif
