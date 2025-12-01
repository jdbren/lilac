#include <mm/mm.h>

#include <lilac/boot.h>
#include <lilac/types.h>
#include <lilac/err.h>
#include <lilac/log.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/syscall.h>
#include <mm/kmm.h>
#include <mm/page.h>
#include <lilac/fs.h>
#include <lilac/libc.h>

static int convert_mmap_flags(int prot, int flags)
{
    int mflags = 0;
    if (prot & PROT_READ) mflags |= VM_READ;
    if (prot & PROT_WRITE) mflags |= VM_WRITE;
    if (prot & PROT_EXEC) mflags |= VM_EXEC;
    if (flags & MAP_SHARED) mflags |= VM_SHARED;
    return mflags;
}

struct vm_desc * find_vma(struct mm_info *mm, uintptr_t addr)
{
    struct vm_desc *vma = mm->mmap;
    while (vma) {
        if (addr >= vma->start && addr < vma->end) {
            return vma;
        }
        vma = vma->vm_next;
    }
    return NULL;
}

struct vm_desc * find_prev_vma(struct mm_info *mm, uintptr_t addr)
{
    struct vm_desc *vma = mm->mmap;
    struct vm_desc *prev = NULL;
    while (vma) {
        if (addr < vma->start)
            return prev;
        prev = vma;
        vma = vma->vm_next;
    }
    return prev;
}

void vma_list_insert(struct vm_desc *vma, struct vm_desc **list)
{
    struct vm_desc *vma_list = *list;
    vma->mm->total_vm += vma->end - vma->start;

    if (vma_list == NULL) {
        vma->vm_next = NULL;
        vma->vm_prev = NULL;
        *list = vma;
        return;
    }

    if (vma->start < vma_list->start) {
        vma->vm_next = vma_list;
        vma->vm_prev = NULL;
        vma_list->vm_prev = vma;
        *list = vma;
        return;
    }

    while (vma_list->vm_next != NULL && vma_list->vm_next->start < vma->start) {
        vma_list = vma_list->vm_next;
    }

    vma->vm_next = vma_list->vm_next;
    vma->vm_prev = vma_list;

    if (vma_list->vm_next != NULL)
        vma_list->vm_next->vm_prev = vma;

    vma_list->vm_next = vma;
}

static struct vm_desc * create_brk_seg(struct mm_info *mm)
{
    klog(LOG_DEBUG, "sbrk: Creating new VMA for brk at %p\n", (void*)mm->start_brk);
    struct vm_desc *vma_list = kzmalloc(sizeof(*vma_list));
    if (!vma_list) {
        return ERR_PTR(-ENOMEM);
    }
    vma_list->mm = mm;
    vma_list->start = mm->start_brk;
    vma_list->end = mm->start_brk;
    vma_list->vm_flags = VM_READ | VM_WRITE;
    vma_list_insert(vma_list, &mm->mmap);
    return vma_list;
}

// Returns start address of a free gap after vma_prev that fits length
static uintptr_t find_gap_after(struct vm_desc *vma_prev,
                                uintptr_t min_addr,
                                size_t length)
{
    uintptr_t start = vma_prev ?
        PAGE_ROUND_UP(vma_prev->end) :
        PAGE_ROUND_UP(min_addr);

    uintptr_t end = start + length;
    if (end < start) // overflow
        return 0;

    if (!vma_prev || !vma_prev->vm_next)
        return start;

    if (end <= vma_prev->vm_next->start)
        return start;

    return 0;
}

// Create a new VMA at or after search_addr
static struct vm_desc * create_new_vma_after(struct mm_info *mm,
    uintptr_t search_addr, size_t length, int flags)
{
    struct vm_desc *iter = find_vma(mm, search_addr);
    struct vm_desc *prev = NULL;

    // If we found a VMA containing search_addr, start after it
    if (iter) {
        prev = iter;
        search_addr = PAGE_ROUND_UP(iter->end);
    }

    for (;;) {
        uintptr_t start = find_gap_after(prev, search_addr, length);
        if (start != 0) {
            struct vm_desc *vma = kzmalloc(sizeof(*vma));
            if (!vma)
                return ERR_PTR(-ENOMEM);

            vma->mm = mm;
            vma->start = start;
            vma->end = start + length;
            vma->vm_flags = flags;

            return vma;
        }

        if (!prev || !prev->vm_next)
            break;

        prev = prev->vm_next;
        search_addr = PAGE_ROUND_UP(prev->end);
    }

    return ERR_PTR(-ENOMEM);
}


static struct vm_desc *create_new_vma_at(struct mm_info *mm,
    uintptr_t vaddr, size_t length, int flags)
{
    uintptr_t end = PAGE_ROUND_UP(vaddr + length);
    if (end < vaddr)
        return ERR_PTR(-EINVAL); // overflow

    if (end >= __USER_MAX_ADDR)
        return ERR_PTR(-EINVAL);

    struct vm_desc *vma = find_vma(mm, vaddr);
    if (vma)
        return ERR_PTR(-EINVAL); // overlapping VMA exists

    struct vm_desc *prev = find_prev_vma(mm, vaddr);

    if (prev && PAGE_ROUND_UP(prev->end) > vaddr)
        return ERR_PTR(-EINVAL); // overlaps previous

    if (prev && prev->vm_next && end > prev->vm_next->start)
        return ERR_PTR(-EINVAL); // overlaps next

    vma = kzmalloc(sizeof(*vma));
    if (!vma)
        return ERR_PTR(-ENOMEM);

    vma->mm = mm;
    vma->start = vaddr;
    vma->end = end;
    vma->vm_flags = flags;

    return vma;
}

// TODO: implement file-backed mmap properly
int do_mmap_file(struct vm_desc *vma, struct file *file, unsigned long offset)
{
    vma->vm_file = file;
    vma->vm_pgoff = offset / PAGE_SIZE;
    if (file->f_op && file->f_op->mmap) {
        vma->vm_flags |= VM_IO;
        return file->f_op->mmap(file, vma);
    }
    return -EOPNOTSUPP;
}


SYSCALL_DECL6(mmap, void*, addr, size_t, length, int, prot,
    int, flags, int, fd, off_t, offset)
{
    long ret = 0;
    struct vm_desc *vma = NULL;
    uintptr_t pgaddr = PAGE_ROUND_DOWN(addr);
    int num_pages = PAGE_ROUND_UP(length + (addr - pgaddr)) / PAGE_SIZE;

    klog(LOG_DEBUG, "mmap (addr: %p, length: %lu, prot: %x, flags: %x, fd: %d, offset: %ld)\n",
        addr, length, prot, flags, fd, offset);

    int mflags = convert_mmap_flags(prot, flags);

    if (flags & MAP_FIXED) {
        if (pgaddr == 0 || (uintptr_t)addr % PAGE_SIZE != 0)
            return -EINVAL;
        vma = create_new_vma_at(current->mm, pgaddr, num_pages * PAGE_SIZE, mflags);
    } else {
        if (pgaddr == 0)
            pgaddr = __USER_MMAP_START; // arbitrary high address
        vma = create_new_vma_after(current->mm, pgaddr, num_pages * PAGE_SIZE, mflags);
    }

    if (IS_ERR(vma)) {
        return PTR_ERR(vma);
    } else if (vma == NULL) {
        return -ENOMEM;
    }

    if (flags & MAP_ANONYMOUS || fd == -1) {
        vma_list_insert(vma, &current->mm->mmap);
        return (long) vma->start;
    }

    // File-backed mmap path
    if (offset % PAGE_SIZE != 0) {
        ret = -EINVAL;
        goto failure;
    }

    struct file *file = get_file_handle(fd);
    if (IS_ERR_OR_NULL(file)) {
        ret = -EBADF;
        goto failure;
    }

    int mode = file->f_mode & O_ACCMODE;
    if (((mode == O_WRONLY) && (mflags & VM_READ)) ||
        ((mode == O_RDONLY) && (mflags & VM_WRITE))) {
        ret = -EACCES;
        goto failure;
    }

    ret = do_mmap_file(vma, file, offset);
    if (ret < 0) {
        goto failure;
    }

    vma_list_insert(vma, &current->mm->mmap);
    return (long)vma->start;

failure:
    if (vma)
        kfree(vma);
    return ret;
}

SYSCALL_DECL2(munmap, void*, addr, size_t, length)
{
    return 0;
}

int brk(void *addr)
{
    struct mm_info *mm = current->mm;
    struct vm_desc *vma = mm->mmap;
    uintptr_t addr_val = (uintptr_t)addr;

    while (vma && vma->start != mm->start_brk) {
        vma = vma->vm_next;
    }

    if (vma == NULL) {
        vma = create_brk_seg(mm);
        if (IS_ERR(vma)) {
            return PTR_ERR(vma);
        }
    }

    if (addr_val < vma->start || addr_val - vma->start > 0xffffff) {
        return -ENOMEM;
    } else if (addr_val > vma->end) {
        uintptr_t vaddr = PAGE_ROUND_UP(addr);
        vma->end = vaddr;
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
    struct vm_desc *vma = mm->mmap;
    uintptr_t end_brk = (uintptr_t)mm->brk;
    klog(LOG_DEBUG, "sbrk: Current break point: %p, Increment: %ld\n", (void*)end_brk, increment);

    while (vma && vma->start != mm->start_brk) {
        vma = vma->vm_next;
    }

    if (vma == NULL) {
        vma = create_brk_seg(mm);
        if (IS_ERR(vma)) {
            return vma;
        }
    }
#ifdef DEBUG_MM
    else {
        klog(LOG_DEBUG, "sbrk: Found existing VMA for brk at %p-%p\n",
            (void*)vma->start, (void*)vma->end);
    }
#endif

    if (increment < 0 || labs(increment) > 0xFFFFFF) {
        klog(LOG_WARN, "sbrk: Invalid increment: %ld\n", increment);
        return ERR_PTR(-ENOMEM); // Invalid increment
    }

    if (end_brk + increment > vma->end) {
        size_t num_pages = PAGE_ROUND_UP(increment) / PAGE_SIZE;
        vma->end += num_pages * PAGE_SIZE;
    } else if (end_brk + increment < vma->start) {
        klog(LOG_WARN, "sbrk: New break point is below the start of the VMA\n");
        return ERR_PTR(-ENOMEM);
    }

    mm->brk += increment;
#ifdef DEBUG_MM
    klog(LOG_DEBUG, "sbrk: New break point: %p\n", (void*)mm->brk);
#endif
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
