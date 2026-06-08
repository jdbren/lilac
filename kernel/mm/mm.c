#include <mm/mm.h>
#include <lilac/boot.h>
#include <lilac/types.h>
#include <lilac/err.h>
#include <lilac/log.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/syscall.h>
#include <lilac/fs.h>
#include <lilac/libc.h>
#include <lilac/sync.h>
#include <mm/kmm.h>
#include <mm/page.h>
#include <mm/tlb.h>

#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"

struct mm_info * alloc_mm_info(void)
{
    struct mm_info *info = kzmalloc(sizeof(*info));
    if (!info) {
        klog(LOG_ERROR, "Failed to allocate mm_info\n");
        return NULL;
    }
    spin_lock_init(&info->page_table_lock);
    rwsem_init(&info->mmap_lock);
    info->ref_count = 1;
    return info;
}

#if defined(DEBUG_MM) || defined(DEBUG_VMA)
static void print_vma_list(struct mm_info *mm)
{
    struct vm_desc *vma = mm->mmap;
    klog(LOG_DEBUG, "VMA list for process %d:\n", current->pid);
    while (vma) {
        klog(LOG_DEBUG, "  VMA: %p-%p, flags: %x\n", (void*)vma->start,
            (void*)vma->end, vma->vm_flags);
        vma = vma->vm_next;
    }
}
#endif

static int convert_mmap_flags(int prot, int flags)
{
    int mflags = 0;
    if (prot & PROT_READ) mflags |= VM_READ;
    if (prot & PROT_WRITE) mflags |= VM_WRITE;
    if (prot & PROT_EXEC) mflags |= VM_EXEC;
    if (flags & MAP_SHARED) mflags |= VM_SHARED;
    return mflags;
}

static int vma_flags_to_user_mem_flags(int flags)
{
    int pt_flags = MEM_PF_USER;
    if (flags & VM_READ) pt_flags |= MEM_PF_READ;
    if (flags & VM_WRITE) pt_flags |= MEM_PF_WRITE;
    if (!(flags & VM_EXEC)) pt_flags |= MEM_PF_NO_EXEC;
    return pt_flags;
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

struct vm_desc * vma_find_prev(struct mm_info *mm, uintptr_t addr)
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

// Check if the entire range [start, end) is covered by VMAs in mm
static bool mm_range_is_mapped(struct mm_info *mm, uintptr_t start,
    uintptr_t end)
{
    struct vm_desc *vma = find_vma(mm, start);
    if (!vma || vma->start > start)
        return false;

    uintptr_t cursor = start;
    while (vma && vma->start < end) {
        if (vma->start > cursor)
            return false; // gap
        if (vma->end >= end)
            return true;
        cursor = vma->end;
        vma = vma->vm_next;
    }
    return false;
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

    while (vma_list->vm_next != NULL &&
           vma_list->vm_next->start < vma->start) {
        vma_list = vma_list->vm_next;
    }

    vma->vm_next = vma_list->vm_next;
    vma->vm_prev = vma_list;

    if (vma_list->vm_next != NULL)
        vma_list->vm_next->vm_prev = vma;

    vma_list->vm_next = vma;

#if defined(DEBUG_MM) || defined(DEBUG_VMA)
    print_vma_list(vma->mm);
#endif
}

static void vma_list_remove(struct vm_desc *vma, struct vm_desc **list)
{
    vma->mm->total_vm -= vma->end - vma->start;

    if (vma->vm_prev)
        vma->vm_prev->vm_next = vma->vm_next;
    else
        *list = vma->vm_next;

    if (vma->vm_next)
        vma->vm_next->vm_prev = vma->vm_prev;

    vma->vm_next = NULL;
    vma->vm_prev = NULL;
}

static struct vm_desc * create_brk_seg(struct mm_info *mm)
{
    klog(LOG_DEBUG, "sbrk: Creating new VMA for brk at %p\n",
        (void*)mm->start_brk);
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

// Create a new VMA at or after search_addr (for MAP_ANON)
static struct vm_desc * vma_create_new_after(struct mm_info *mm,
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
                goto error;

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

error:
    return ERR_PTR(-ENOMEM);
}


__maybe_unused static
struct vm_desc *vma_create_new_at(struct mm_info *mm, uintptr_t vaddr,
    size_t length, int flags)
{
    uintptr_t end = PAGE_ROUND_UP(vaddr + length);
    if (end < vaddr)
        return ERR_PTR(-EINVAL); // overflow

    if (end >= __USER_MAX_ADDR)
        return ERR_PTR(-EINVAL);

    struct vm_desc *vma = find_vma(mm, vaddr);
    if (vma)
        return ERR_PTR(-EINVAL); // overlapping VMA exists

    struct vm_desc *prev = vma_find_prev(mm, vaddr);

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

static int vma_split(struct vm_desc *vma, uintptr_t start, uintptr_t end)
{
#ifdef DEBUG_MM
    klog(LOG_DEBUG, "Splitting VMA %p-%p into %p-%p and %p-%p\n",
        (void*)vma->start, (void*)vma->end, (void*)vma->start, (void*)start, (void*)end, (void*)vma->end);
#endif
    struct vm_desc *tail = kzmalloc(sizeof(*tail));
    if (!tail)
        return -ENOMEM;
    *tail = *vma;
    tail->start = end;
    vma->end = start;
    // Insert the tail after the current vma
    tail->vm_prev = vma;
    tail->vm_next = vma->vm_next;
    if (vma->vm_next)
        vma->vm_next->vm_prev = tail;
    vma->vm_next = tail;
    return 0;
}

// Unmap any VMAs overlapping [start, end), splitting partially-covered ones.
// Returns 1 if any VMAs were unmapped, 0 if no overlaps, or a negative error code.
static int vma_unmap_range(struct vm_desc *vma, uintptr_t start, uintptr_t end)
{
    int err;

    while (vma && vma->end <= start)
        vma = vma->vm_next;

    if (!vma || vma->start >= end)
        return 0; // no overlaps

    while (vma && vma->start < end) {
        struct vm_desc *next = vma->vm_next;

        if (vma->start >= start && vma->end <= end) {
            // Entirely contained
            vma_list_remove(vma, &vma->mm->mmap);
            kfree(vma);
        } else if (vma->start < start && vma->end > end) {
            // VMA spans beyond both sides
            err = vma_split(vma, start, end);
            if (err < 0) {
                do_raise(current, SIGKILL);
                return err;
            }
            vma->mm->total_vm -= (end - start);
            break; // no more overlaps possible
        } else if (vma->start < start) {
            // Overlaps at the end
            vma->mm->total_vm -= vma->end - start;
            vma->end = start;
        } else {
            // Overlaps at the beginning
            vma->mm->total_vm -= end - vma->start;
            vma->start = end;
        }

        vma = next;
    }

    return 1;
}

static int mmap_unmap_range(struct mm_info *mm, uintptr_t start, uintptr_t end)
{
    int err = 0;
    struct tlb_inval tlb = {
        .mm = mm,
        .start = start,
        .end = end,
        .full = false,
    };

    klog(LOG_DEBUG, "mmap_unmap_range: unmapping range %p - %p\n",
        (void*)start, (void*)end);

    err = vma_unmap_range(mm->mmap, start, end);
    if (err <= 0)
        goto error;

    acquire_lock(&mm->page_table_lock);
    drop_user_page_range(start, end - start);
    arch_tlb_flush_mmu(&tlb);
    release_lock(&mm->page_table_lock);

error:
    return err;
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

    klog(LOG_DEBUG, "mmap (addr: %p, length: %lu, prot: 0x%x, flags: 0x%x, fd: %d,"
        " offset: %ld)\n", addr, length, prot, flags, fd, offset);

    if (!(flags & 3))
        return -EINVAL;

    int mflags = convert_mmap_flags(prot, flags);

    if (flags & MAP_FIXED) {
        if (pgaddr == 0 || (uintptr_t)addr % PAGE_SIZE != 0)
            return -EINVAL;
        uintptr_t map_end = pgaddr + (uintptr_t)num_pages * PAGE_SIZE;
        if (map_end < pgaddr || map_end >= __USER_MAX_ADDR)
            return -EINVAL;

        vma = kzmalloc(sizeof(*vma));
        if (!vma)
            return -ENOMEM;
        vma->mm = current->mm;
        vma->start = pgaddr;
        vma->end = map_end;
        vma->vm_flags = mflags;

        mmap_write_lock(current->mm);
        mmap_unmap_range(current->mm, pgaddr, map_end);
    } else {
        if (pgaddr == 0)
            pgaddr = __USER_MMAP_START; // arbitrary high address
        klog(LOG_DEBUG, "mmap anonymous: num_pages = %d\n", num_pages);
        mmap_write_lock(current->mm);
        vma = vma_create_new_after(current->mm, pgaddr,
            num_pages * PAGE_SIZE, mflags);
    }

    if (IS_ERR(vma)) {
        ret = PTR_ERR(vma);
        klog(LOG_ERROR, "mmap: Failed to create VMA: %ld\n", ret);
        goto out;
    } else if (vma == NULL) {
        klog(LOG_ERROR, "mmap: Failed to create VMA: unknown error\n");
        ret = -ENOMEM;
        goto out;
    }

    if (flags & MAP_ANONYMOUS || fd == -1) {
        vma_list_insert(vma, &current->mm->mmap);
        ret = (long) vma->start;
        goto success;
    }

    // File-backed mmap path
    if (offset % PAGE_SIZE != 0) {
        ret = -EINVAL;
        goto free_vma;
    }

    struct file *file = get_file_handle(fd);
    if (IS_ERR_OR_NULL(file)) {
        ret = -EBADF;
        goto free_vma;
    }

    int mode = file->f_mode & O_ACCMODE;
    if (((mode == O_WRONLY) && (mflags & VM_READ)) ||
        ((mode == O_RDONLY) && (mflags & VM_WRITE))) {
        ret = -EACCES;
        goto free_vma;
    }

    ret = do_mmap_file(vma, file, offset);
    if (ret < 0) {
        goto free_vma;
    }

    vma_list_insert(vma, &current->mm->mmap);
    ret = (long)vma->start;
    goto success;

free_vma:
    if (vma)
        kfree(vma);
out:
success:
    mmap_write_unlock(current->mm);
    return ret;
}

SYSCALL_DECL2(munmap, void*, addr, size_t, length)
{
    klog(LOG_DEBUG, "munmap (addr: %p, length: %lu)\n", addr, length);

    uintptr_t pgaddr = (uintptr_t)addr;
    if (length == 0 || length >= 0x1000000000UL || pgaddr == 0
            || pgaddr >= __USER_MAX_ADDR || pgaddr % PAGE_SIZE) {
        return -EINVAL;
    }

    uintptr_t end = PAGE_ROUND_UP(pgaddr + length);
    if (end < pgaddr || end > __USER_MAX_ADDR) {
        return -EINVAL;
    }

    struct mm_info *mm = current->mm;
    long ret;

    mmap_write_lock(mm);
    ret = mmap_unmap_range(mm, pgaddr, end);
    mmap_write_unlock(mm);
    return ret;
}

SYSCALL_DECL5(mremap, void*, old_addr, size_t, old_length, size_t, new_length,
    int, flags, void*, new_addr)
{
    // TODO
    return -ENOSYS;
}

int brk(void *addr)
{
    struct mm_info *mm = current->mm;
    struct vm_desc *vma = mm->mmap;
    uintptr_t addr_val = (uintptr_t)addr;

    if (addr_val < mm->start_brk)
        return 0;

    mmap_write_lock(mm);

    while (vma && vma->start != mm->start_brk) {
        vma = vma->vm_next;
    }

    if (vma == NULL) {
        vma = create_brk_seg(mm);
        if (IS_ERR(vma)) {
            mmap_write_unlock(mm);
            return PTR_ERR(vma);
        }
    }

    if (addr_val < vma->start || addr_val - vma->start > 0xffffff) {
        mmap_write_unlock(mm);
        return -ENOMEM;
    } else if (addr_val > vma->end) {
        uintptr_t vaddr = PAGE_ROUND_UP(addr);
        vma->end = vaddr;
    }

    mm->brk = addr_val;
    mmap_write_unlock(mm);
    return 0;
}

SYSCALL_DECL1(brk, void*, addr)
{
    klog(LOG_DEBUG, "brk called with addr: %p\n", addr);
    int ret = brk(addr);
    klog(LOG_DEBUG, "brk result: %d, new brk: %p\n", ret, current->mm->brk);
    return (uintptr_t)current->mm->brk;
}

void * sbrk(intptr_t increment)
{
    struct mm_info *mm = current->mm;
    struct vm_desc *vma = mm->mmap;
    uintptr_t end_brk = (uintptr_t)mm->brk;
    klog(LOG_DEBUG, "sbrk: Current break point: %p, Increment: %ld\n",
        (void*)end_brk, increment);

    mmap_write_lock(mm);

    while (vma && vma->start != mm->start_brk) {
        vma = vma->vm_next;
    }

    if (vma == NULL) {
        vma = create_brk_seg(mm);
        if (IS_ERR(vma)) {
            mmap_write_unlock(mm);
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
        mmap_write_unlock(mm);
        klog(LOG_WARN, "sbrk: Invalid increment: %ld\n", increment);
        return ERR_PTR(-ENOMEM); // Invalid increment
    }

    if (end_brk + increment > vma->end) {
        size_t num_pages = PAGE_ROUND_UP(increment) / PAGE_SIZE;
        vma->end += num_pages * PAGE_SIZE;
    } else if (end_brk + increment < vma->start) {
        mmap_write_unlock(mm);
        klog(LOG_WARN, "sbrk: New break point is below the start of the VMA\n");
        return ERR_PTR(-ENOMEM);
    }

    mm->brk += increment;
    mmap_write_unlock(mm);
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
SYSCALL_DECL2(memfd_create, const char *, name, __unused unsigned int, flags)
{
    klog(LOG_WARN, "memfd_create is not fully implemented, using a temporary"
        "file as a placeholder\n");
    static int n = 0;
    char buf[64];
    while (*name && *name == '/')
        name++;
    snprintf(buf, 64, "/tmp/%s/%d", name, n);
    struct file *f = vfs_open(buf, O_CREAT, S_IREAD|S_IWRITE);
    return get_next_fd(current->files, f);
}

static int mm_update_region(struct vm_desc *vma, uintptr_t pgaddr,
    uintptr_t end, int prot_flags)
{
    if (!vma) return -EINVAL;
    // If start address is not the beginning of the VMA
    if (vma->start < pgaddr) {
        int ret = vma_split(vma, pgaddr, pgaddr);
        if (ret < 0)
            return ret;
        vma = vma->vm_next;
        klog(LOG_DEBUG, "mm_update_region: split VMA, new VMA at %p-%p\n",
            (void*)vma->start, (void*)vma->end);
        assert(vma && vma->start == pgaddr);
    }

    while (vma && vma->end <= end) {
        vma->vm_flags = (vma->vm_flags & ~VM_PROT_MASK) | prot_flags;
        if (vma->end == end)
            break;
        vma = vma->vm_next;
    }

    if (!vma)
        return -ENOMEM;

    // If the end address is not the end of the VMA
    if (vma->start < end) {
        int ret = vma_split(vma, end, end);
        if (ret < 0)
            return ret;
        vma->vm_flags = (vma->vm_flags & ~VM_PROT_MASK) | prot_flags;
    }

    int mem_flags = vma_flags_to_user_mem_flags(prot_flags);

    acquire_lock(&vma->mm->page_table_lock);
    update_user_page_range(pgaddr, end - pgaddr, mem_flags);
    release_lock(&vma->mm->page_table_lock);

    return 0;
}

static int do_mprotect_locked(struct mm_info *mm, uintptr_t pgaddr,
    uintptr_t end, int prot_flags)
{
    if (!mm_range_is_mapped(mm, pgaddr, end))
        return -ENOMEM;

    struct vm_desc *vma = find_vma(mm, pgaddr);
    if (!vma)
        return -ENOMEM;

    return mm_update_region(vma, pgaddr, end, prot_flags);
}

static int do_mprotect(struct mm_info *mm, uintptr_t pgaddr,
    uintptr_t end, int prot_flags)
{
    mmap_write_lock(mm);
    long ret = do_mprotect_locked(mm, pgaddr, end, prot_flags);
    mmap_write_unlock(mm);
    return ret;
}

SYSCALL_DECL3(mprotect, void *, addr, size_t, len, int, prot)
{
    klog(LOG_DEBUG, "mprotect addr: %p, len: %lu, prot: %x\n",
        addr, len, prot);
    uintptr_t pgaddr = (uintptr_t)addr;
    if (pgaddr & (PAGE_SIZE-1) || pgaddr >= __USER_MAX_ADDR)
        return -EINVAL;

    uintptr_t end = PAGE_ROUND_UP(pgaddr + len);
    if (end < pgaddr || end > __USER_MAX_ADDR)
        return -EINVAL;

    if (len == 0)
        return 0;

    return do_mprotect(current->mm, pgaddr, end, convert_mmap_flags(prot, 0));
}
