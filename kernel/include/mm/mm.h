#ifndef _LILAC_MM_H_
#define _LILAC_MM_H_

#include <lilac/types.h>
#include <lilac/sync.h>

#include <user/mman-bits.h>

#define VM_NONE         0x0000
#define VM_READ         0x0001
#define VM_WRITE        0x0002
#define VM_EXEC         0x0004
#define VM_SHARED       0x0008

struct vm_desc {
    uintptr_t start;
    uintptr_t end;
    struct mm_info *mm;

    /* list sorted by address */
    struct vm_desc *vm_next;
    // rb_node_t vm_rb;

    u32 vm_prot;
    u32 vm_flags;

    unsigned long vm_pgoff;
    struct file *vm_file;
};

int umem_alloc(uintptr_t vaddr, int num_pages);
void vma_list_insert(struct vm_desc *vma, struct vm_desc **list);

void * sbrk(intptr_t increment);


enum fault_flags {
    FAULT_WRITE     = 0x1,
    FAULT_INSTR     = 0x2,
    FAULT_USER      = 0x4,
    FAULT_PTE_EXIST = 0x8,
};

enum fault_return {
    FAULT_SUCCESS,
    FAULT_PROT_VIOLATION,
    FAULT_FILE_ERROR,
};

int mm_fault(struct vm_desc *vma, uintptr_t addr, unsigned long flags);

#endif // _LILAC_MM_H_
