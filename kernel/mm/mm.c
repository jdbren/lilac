#include <lilac/mm.h>

#include <lilac/types.h>
#include <lilac/err.h>
#include <lilac/log.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/syscall.h>
#include <lilac/kmm.h>
#include <lilac/fs.h>
#include <lilac/libc.h>


void* mmap_internal(void *addr, unsigned long len, u32 prot, u32 flags,
    struct file *file, unsigned long offset)
{
    struct mm_info *mm = current->mm;
    struct vm_desc *vma_list = mm->mmap;

    while (vma_list->vm_next != NULL && vma_list->vm_next->start < (uintptr_t)addr) {
        vma_list = vma_list->vm_next;
    }

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

int brk(void *addr)
{
    struct mm_info *mm = current->mm;
    struct vm_desc *vma_list = mm->mmap;
    uintptr_t addr_val = (uintptr_t)addr;

    // Find the VMA that contains the current break point
    while (vma_list && vma_list->end <= (uintptr_t)mm->brk) {
        vma_list = vma_list->vm_next;
    }

    if (vma_list == NULL) {
        return -ENOMEM; // No VMA found
    }

    // Check if the new break address is valid
    if (addr_val < vma_list->start || addr_val - vma_list->start > 0xffffff) {
        return -ENOMEM; // Invalid address
    } else if (addr_val > vma_list->end) {
        uintptr_t vaddr = PAGE_ROUND_UP(addr);
        umem_alloc(vma_list->end, addr_val - vma_list->end);
        vma_list->end = vaddr;
    }

    mm->brk = addr_val;
    return 0;
}

SYSCALL_DECL1(brk, void*, addr)
{
    klog(LOG_DEBUG, "brk called with addr: %p\n", addr);
    int ret = brk(addr);
    if (ret < 0)
        return ret;
    return (uintptr_t)current->mm->brk;
}

void * sbrk(intptr_t increment)
{
    struct mm_info *mm = current->mm;
    struct vm_desc *vma_list = mm->mmap;

    uintptr_t end_brk = (uintptr_t)mm->brk;
#ifdef DEBUG_MM
    klog(LOG_DEBUG, "sbrk: Current break point: %p, Increment: %ld\n", (void*)end_brk, increment);
#endif
    // Find the VMA that contains the current break point
    while (vma_list && vma_list->end < end_brk) {
        vma_list = vma_list->vm_next;
    }

    if (vma_list == NULL) {
        klog(LOG_WARN, "sbrk: No VMA found for current break point\n");
        return ERR_PTR(-ENOMEM); // No VMA found
    }

    if (increment < 0 || (mm->brk - vma_list->start + increment) > 0xFFFFFF) {
        klog(LOG_WARN, "sbrk: Invalid increment: %ld\n", increment);
        return ERR_PTR(-ENOMEM); // Invalid increment
    }

    if (end_brk + increment > vma_list->end) {
        size_t num_pages = PAGE_ROUND_UP(increment) / PAGE_SIZE;
        umem_alloc(vma_list->end, num_pages);
        vma_list->end += num_pages * PAGE_SIZE;
    } else if (end_brk + increment < vma_list->start) {
        klog(LOG_WARN, "sbrk: New break point is below the start of the VMA\n");
        return ERR_PTR(-ENOMEM);
    }

    mm->brk += increment;
    return (void *)end_brk;
}

SYSCALL_DECL1(sbrk, intptr_t, increment)
{
    void *_brk = sbrk(increment);
    if (IS_ERR(_brk)) {
        return PTR_ERR(_brk);
    }
    return (uintptr_t)_brk;
}

// TODO: flimsy HACK, not POSIX
SYSCALL_DECL2(memfd_create, const char *, name, unsigned int, flags)
{
    static int n = 0;
    char buf[64];
    while (*name && *name == '/')
        name++;
    snprintf(buf, 64, "/tmp/%s/%d", name, n);
    struct file *f = vfs_open(buf, O_CREAT, S_IREAD|S_IWRITE);
    return get_next_fd(&current->fs.files, f);
}
