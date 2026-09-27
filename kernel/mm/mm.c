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
#include <lilac/uaccess.h>
#include <lilac/fdtable.h>
#include <fs/tmpfs.h>
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

int vma_flags_to_user_mem_flags(int flags)
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

// Find the lowest free range of length bytes at or after search_addr.
// Returns 0 if no gap fits.
static uintptr_t find_free_area(struct mm_info *mm, uintptr_t search_addr, size_t length)
{
    uintptr_t start = PAGE_ROUND_UP(search_addr);

    for (struct vm_desc *vma = mm->mmap; vma; vma = vma->vm_next) {
        if (vma->end <= start)
            continue;
        if (start + length <= vma->start && start + length >= start)
            break;
        start = PAGE_ROUND_UP(vma->end);
    }

    if (start + length < start || start + length > __USER_MAX_ADDR)
        return 0;
    return start;
}

// Create a new VMA at or after search_addr (for MAP_ANON)
static struct vm_desc * vma_create_new_after(struct mm_info *mm,
    uintptr_t search_addr, size_t length, int flags)
{
    uintptr_t start = find_free_area(mm, search_addr, length);
    if (start == 0)
        return ERR_PTR(-ENOMEM);

    struct vm_desc *vma = kzmalloc(sizeof(*vma));
    if (!vma)
        return ERR_PTR(-ENOMEM);

    vma->mm = mm;
    vma->start = start;
    vma->end = start + length;
    vma->vm_flags = flags;

    return vma;
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
    if (tail->vm_file)
        fget(tail->vm_file);
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

// Write any dirty pages in [start, end) of a MAP_SHARED file VMA back to disk.
int writeback_vma_range(struct vm_desc *vma, uintptr_t start, uintptr_t end)
{
    if (!vma->vm_file || !(vma->vm_flags & VM_SHARED) || (vma->vm_flags & VM_IO))
        return 0;

    start = MAX(start, vma->start);
    end = MIN(end, vma->end);

    lock_page_table(vma->mm);
    for (uintptr_t pgaddr = PAGE_ROUND_DOWN(start); pgaddr < end; pgaddr += PAGE_SIZE) {
        if (get_and_clear_pte_dirty((void*)pgaddr) <= 0)
            continue;

        if (pgaddr < vma->seg_vaddr)
            continue;
        size_t off_in_seg = pgaddr - vma->seg_vaddr;
        if (off_in_seg >= vma->vm_fsize)
            continue;

        size_t bytes = MIN((size_t)PAGE_SIZE, vma->vm_fsize - off_in_seg);
        uintptr_t phys = __walk_pages((void*)pgaddr);
        if (!phys)
            continue;

        vfs_write_at(vma->vm_file, phys_to_virt(phys), bytes,
            vma->seg_offset + off_in_seg);
    }
    unlock_page_table(vma->mm);
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

        writeback_vma_range(vma, start, end);

        if (vma->start >= start && vma->end <= end) {
            // Entirely contained
            vma_list_remove(vma, &vma->mm->mmap);
            if (vma->vm_file)
                fput(vma->vm_file);
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
    tlb_shootdown(&tlb, current);
    release_lock(&mm->page_table_lock);

error:
    return err;
}

int do_mmap_file(struct vm_desc *vma, struct file *file, unsigned long offset)
{
    fget(file);
    vma->vm_file = file;
    vma->vm_pgoff = offset / PAGE_SIZE;
    if (file->f_op && file->f_op->mmap) {
        vma->vm_flags |= VM_IO;
        return file->f_op->mmap(file, vma);
    }

    struct inode *inode = file->f_inode;
    loff_t fsize;
    acquire_lock(&inode->i_lock);
    fsize = inode->i_size;
    release_lock(&inode->i_lock);

    vma->seg_vaddr = vma->start;
    vma->seg_offset = offset;
    // faults past EOF are zero-filled
    vma->vm_fsize = (offset >= (unsigned long)fsize) ? 0
        : MIN((unsigned long)(fsize - offset), vma->end - vma->start);
    return 0;
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

    if (length == 0)
        return -EINVAL;
    if (!(flags & MAP_SHARED) && !(flags & MAP_PRIVATE))
        return -EINVAL;
    if ((flags & MAP_SHARED) && (flags & MAP_PRIVATE))
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
        fput(file);
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
    return ret < 0 ? ret : 0;
}

// After growing a file VMA, extend the file-backed part if it was previously
// clamped by the mapping size rather than by EOF.
static void vma_grow_fsize(struct vm_desc *vma, uintptr_t old_end)
{
    if (!vma->vm_file || (vma->vm_flags & VM_IO))
        return;
    if (vma->vm_fsize < old_end - vma->seg_vaddr)
        return;

    struct inode *inode = vma->vm_file->f_inode;
    loff_t fsize;
    acquire_lock(&inode->i_lock);
    fsize = inode->i_size;
    release_lock(&inode->i_lock);

    vma->vm_fsize = (vma->seg_offset >= (unsigned long)fsize) ? 0
        : MIN((unsigned long)fsize - vma->seg_offset, vma->end - vma->seg_vaddr);
}

static long do_mremap(struct mm_info *mm, uintptr_t old_addr, size_t old_len,
    size_t new_len, int flags, uintptr_t new_addr)
{
    uintptr_t old_end = old_addr + old_len;
    uintptr_t dest;
    long err;

    struct vm_desc *vma = find_vma(mm, old_addr);
    if (!vma || vma->end < old_end)
        return -EFAULT;

    if ((flags & MREMAP_FIXED) &&
            new_addr < old_end && old_addr < new_addr + new_len)
        return -EINVAL;

    if (new_len < old_len) {
        err = mmap_unmap_range(mm, old_addr + new_len, old_end);
        if (err < 0)
            return err;
        old_len = new_len;
        old_end = old_addr + old_len;
        vma = find_vma(mm, old_addr);
        if (!vma)
            return -EFAULT;
    }

    if (!(flags & MREMAP_FIXED)) {
        if (new_len == old_len)
            return old_addr;

        // Try to grow in place
        if (old_end == vma->end && !(vma->vm_flags & VM_IO) &&
                (!vma->vm_next || vma->vm_next->start >= old_addr + new_len) &&
                old_addr + new_len <= __USER_MAX_ADDR) {
            vma->end = old_addr + new_len;
            mm->total_vm += new_len - old_len;
            vma_grow_fsize(vma, old_end);
            return old_addr;
        }

        if (!(flags & MREMAP_MAYMOVE))
            return -ENOMEM;
    }

    if ((vma->vm_flags & VM_IO) && new_len > old_len)
        return -EINVAL;

    if (flags & MREMAP_FIXED) {
        dest = new_addr;
        err = mmap_unmap_range(mm, dest, dest + new_len);
        if (err < 0)
            return err;
        // The unmap may have split the VMA containing the old range
        vma = find_vma(mm, old_addr);
        if (!vma)
            return -EFAULT;
    } else {
        dest = find_free_area(mm, __USER_MMAP_START, new_len);
        if (dest == 0)
            return -ENOMEM;
    }

    // Isolate [old_addr, old_end) into its own VMA
    if (vma->start < old_addr) {
        err = vma_split(vma, old_addr, old_addr);
        if (err < 0)
            return err;
        vma = vma->vm_next;
    }
    if (vma->end > old_end) {
        err = vma_split(vma, old_end, old_end);
        if (err < 0)
            return err;
    }

    vma_list_remove(vma, &mm->mmap);

    struct tlb_inval tlb = {
        .mm = mm,
        .start = old_addr,
        .end = old_end,
        .full = false,
    };
    lock_page_table(mm);
    err = move_user_page_range(old_addr, dest, old_len);
    if (err < 0) {
        unlock_page_table(mm);
        vma_list_insert(vma, &mm->mmap);
        return err;
    }
    tlb_shootdown(&tlb, current);
    unlock_page_table(mm);

    // The file reference, if any, moves with the VMA
    vma->seg_vaddr = vma->seg_vaddr - old_addr + dest;
    vma->start = dest;
    vma->end = dest + new_len;
    vma_list_insert(vma, &mm->mmap);
    if (new_len > old_len)
        vma_grow_fsize(vma, dest + old_len);

    return dest;
}

SYSCALL_DECL5(mremap, void*, old_addr, size_t, old_length, size_t, new_length,
    int, flags, void*, new_addr)
{
    klog(LOG_DEBUG, "mremap (old_addr: %p, old_length: %lu, new_length: %lu, "
        "flags: 0x%x, new_addr: %p)\n", old_addr, old_length, new_length,
        flags, new_addr);

    uintptr_t old = (uintptr_t)old_addr;
    uintptr_t new = (uintptr_t)new_addr;

    if (flags & ~(MREMAP_MAYMOVE | MREMAP_FIXED))
        return -EINVAL; // MREMAP_DONTUNMAP is unsupported
    if ((flags & MREMAP_FIXED) && !(flags & MREMAP_MAYMOVE))
        return -EINVAL;
    if (old == 0 || old % PAGE_SIZE || old_length == 0 || new_length == 0)
        return -EINVAL;

    size_t old_len = PAGE_ROUND_UP(old_length);
    size_t new_len = PAGE_ROUND_UP(new_length);
    if (old_len < old_length || new_len < new_length)
        return -EINVAL;
    if (old + old_len < old || old + old_len > __USER_MAX_ADDR)
        return -EINVAL;
    if (flags & MREMAP_FIXED) {
        if (new == 0 || new % PAGE_SIZE ||
                new + new_len < new || new + new_len > __USER_MAX_ADDR)
            return -EINVAL;
    }

    struct mm_info *mm = current->mm;
    mmap_write_lock(mm);
    long ret = do_mremap(mm, old, old_len, new_len, flags, new);
    mmap_write_unlock(mm);
    return ret;
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

#define MFD_NAME_MAX 249

SYSCALL_DECL2(memfd_create, const char *, name, unsigned int, flags)
{
    char kname[MFD_NAME_MAX + 1];
    long fd;

    if (flags & ~(MFD_CLOEXEC | MFD_ALLOW_SEALING))
        return -EINVAL; // no hugetlb

    int len = strncpy_from_user(kname, name, sizeof(kname));
    if (len < 0)
        return len;
    if (len == 0 || kname[len - 1] != '\0')
        return -EINVAL;

    struct file *f = tmpfs_anon_file(kname);
    if (IS_ERR(f))
        return PTR_ERR(f);

    fd = get_next_fd(current->files, f);
    if (fd < 0) {
        fput(f);
        return fd;
    }
    if (flags & MFD_CLOEXEC)
        set_cloexec(current->files, fd);

    return fd;
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

    struct mm_info *mm = vma->mm;
    int mem_flags = vma_flags_to_user_mem_flags(prot_flags);

    // Private pages may still be shared after fork, so leave them read-only
    // and let the first write go through do_cow_fault
    acquire_lock(&mm->page_table_lock);
    for (vma = find_vma(mm, pgaddr); vma && vma->start < end; vma = vma->vm_next) {
        int flags = mem_flags;
        if ((flags & MEM_PF_WRITE) && !(vma->vm_flags & (VM_SHARED | VM_IO)))
            flags = (flags & ~MEM_PF_WRITE) | MEM_PF_READ;
        uintptr_t s = MAX(vma->start, pgaddr);
        update_user_page_range(s, MIN(vma->end, end) - s, flags);
    }
    release_lock(&mm->page_table_lock);

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

SYSCALL_DECL3(msync, void *, addr, size_t, len, int, flags)
{
    klog(LOG_DEBUG, "msync addr: %p, len: %lu, flags: %x\n", addr, len, flags);
    uintptr_t pgaddr = (uintptr_t)addr;
    if (pgaddr & (PAGE_SIZE-1) || pgaddr >= __USER_MAX_ADDR)
        return -EINVAL;
    if ((flags & MS_ASYNC) && (flags & MS_SYNC))
        return -EINVAL;

    uintptr_t end = PAGE_ROUND_UP(pgaddr + len);
    if (end < pgaddr || end > __USER_MAX_ADDR)
        return -EINVAL;

    if (len == 0)
        return 0;

    struct mm_info *mm = current->mm;
    mmap_read_lock(mm);
    if (!mm_range_is_mapped(mm, pgaddr, end)) {
        mmap_read_unlock(mm);
        return -ENOMEM;
    }

    struct vm_desc *vma = find_vma(mm, pgaddr);
    while (vma && vma->start < end) {
        writeback_vma_range(vma, pgaddr, end);
        vma = vma->vm_next;
    }
    mmap_read_unlock(mm);

    return 0;
}
