#include <lilac/mm.h>

#include <lilac/types.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <mm/kmalloc.h>


void* mmap_internal(void *addr, unsigned long len, u32 prot, u32 flags,
    struct file *file, unsigned long offset)
{
    struct mm_info *mm = current->mm;
    struct vm_desc *vma_list = mm->mmap;

    while (vma_list->vm_next != NULL && vma_list->vm_next->start < (uintptr_t)addr) {
        vma_list = vma_list->vm_next;
    }

    struct vm_desc *vma = kmalloc(sizeof(struct vm_desc));
    return NULL;
}

void vma_list_insert(struct vm_desc *vma, struct vm_desc **list)
{
    struct vm_desc *vma_list = *list;
    vma->mm->total_vm += vma->end - vma->start;

    if (vma_list == NULL) {
        *list = vma;
        return;
    }

    while (vma_list->vm_next != NULL && vma_list->vm_next->start < vma->start) {
        vma_list = vma_list->vm_next;
    }

    vma->vm_next = vma_list->vm_next;
    vma_list->vm_next = vma;
}
