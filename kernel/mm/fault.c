#include <mm/mm.h>
#include <mm/kmm.h>
#include <mm/page.h>
#include <lilac/panic.h>
#include <lilac/fs.h>
#include <lilac/sync.h>

#ifdef DEBUG_MM
unsigned long mm_dbg_fault_file_pages_alloc = 0;
unsigned long mm_dbg_fault_anon_pages_alloc = 0;
unsigned long mm_dbg_fork_copy_pages_alloc = 0;
unsigned long mm_dbg_unmap_requested_pages = 0;
unsigned long mm_dbg_unmap_data_pages_freed = 0;
unsigned long mm_dbg_page_table_pages_alloc = 0;
unsigned long mm_dbg_page_table_pages_freed = 0;
unsigned long mm_dbg_pgd_pages_alloc = 0;
unsigned long mm_dbg_reclaim_pgd_pages_freed = 0;
#endif

static bool check_access(struct vm_desc *vma, unsigned int flags)
{
    if (flags & FAULT_USER && !(vma->vm_flags & (VM_READ | VM_WRITE | VM_EXEC))) {
        return false;
    }
    if (flags & FAULT_WRITE && !(vma->vm_flags & VM_WRITE)) {
        return false;
    }
    if (flags & FAULT_INSTR && !(vma->vm_flags & VM_EXEC)) {
        return false;
    }
    return true;
}

static int do_file_fault(struct vm_desc *vma, uintptr_t pgaddr)
{
    struct file *f = vma->vm_file;
    uintptr_t seg_vaddr  = vma->seg_vaddr;   /* exact ELF p_vaddr */
    size_t    seg_offset = vma->seg_offset;  /* exact ELF p_offset */
    size_t    fsize      = vma->vm_fsize;    /* number of bytes in file for this segment */

    u8 *buf = get_free_page();
    if (!buf)
        return FAULT_OOM;
#ifdef DEBUG_MM
    mm_dbg_fault_file_pages_alloc++;
#endif

    // Determine where the segment's file-backed region intersects this page.
    size_t start_in_page = 0;
    if (pgaddr < seg_vaddr) {
        start_in_page = (size_t)(seg_vaddr - pgaddr);
    }

    size_t off_in_seg = (pgaddr + start_in_page > seg_vaddr)
                        ? (size_t)((pgaddr + start_in_page) - seg_vaddr)
                        : 0;

    if (off_in_seg >= fsize) {
        memset(buf, 0, PAGE_SIZE);
        goto map_page_out;
    }

    /* We will place file bytes into buf at offset start_in_page and read at most
       PAGE_SIZE - start_in_page bytes (the rest of page is before/after this file region). */
    size_t bytes_to_read = fsize - off_in_seg;
    if (bytes_to_read > PAGE_SIZE - start_in_page)
        bytes_to_read = PAGE_SIZE - start_in_page;

    if (start_in_page > 0)
        memset(buf, 0, start_in_page);

    size_t file_offset = seg_offset + off_in_seg;
#ifdef DEBUG_MM
    klog(LOG_DEBUG, "File-backed fault: pgaddr=%lx start_in_page=%lx off_in_seg=%lx file_offset=%lx read=%lx\n",
         pgaddr, start_in_page, off_in_seg, file_offset, bytes_to_read);
#endif

    ssize_t bytes = vfs_read_at(f, buf + start_in_page, bytes_to_read, file_offset);
    if (bytes < 0) {
        free_page(buf);
        return FAULT_FILE_ERROR;
    }

    if ((size_t)bytes < bytes_to_read)
        memset(buf + start_in_page + bytes, 0, bytes_to_read - (size_t)bytes);

    size_t filled = start_in_page + (size_t)bytes;
    if (filled < PAGE_SIZE)
        memset(buf + filled, 0, PAGE_SIZE - filled);

map_page_out:
    acquire_lock(&vma->mm->page_table_lock);
    map_page((void *)virt_to_phys(buf), (void *)pgaddr,
        vma_flags_to_user_mem_flags(vma->vm_flags));
    release_lock(&vma->mm->page_table_lock);

    return FAULT_SUCCESS;
}

static int do_anon_fault(struct vm_desc *vma, uintptr_t pgaddr)
{
    void *page = get_zeroed_pages(1, ALLOC_NORMAL);
    if (!page)
        return FAULT_OOM;
#ifdef DEBUG_MM
    mm_dbg_fault_anon_pages_alloc++;
#endif
    acquire_lock(&vma->mm->page_table_lock);
    map_page((void*)virt_to_phys(page), (void*)pgaddr,
        vma_flags_to_user_mem_flags(vma->vm_flags));
    release_lock(&vma->mm->page_table_lock);
    return FAULT_SUCCESS;
}

// Handle a write fault on a page shared by copy_vm_area() at fork time
static int do_cow_fault(struct vm_desc *vma, uintptr_t pgaddr)
{
    struct mm_info *mm = vma->mm;

    lock_page_table(mm);
    uintptr_t old_phys = __walk_pages((void*)pgaddr);
    if (!old_phys) {
        unlock_page_table(mm);
        return FAULT_OOM;
    }

    int mem_pflags = vma_flags_to_user_mem_flags(vma->vm_flags) | MEM_PF_WRITE;

    struct page *old_page = phys_to_page(old_phys);
    if (old_page->refcount == 1) {
        // If the page was already copied and is now only referenced by this process
        update_user_page_range(pgaddr, PAGE_SIZE, mem_pflags);
        unlock_page_table(mm);
        return FAULT_SUCCESS;
    }

    void *new_page = get_free_page();
    if (!new_page) {
        unlock_page_table(mm);
        return FAULT_OOM;
    }
    memcpy(new_page, phys_to_virt(old_phys), PAGE_SIZE);
    remap_page((void*)virt_to_phys(new_page), (void*)pgaddr, mem_pflags);
    unlock_page_table(mm);

    put_page(old_page);
    return FAULT_SUCCESS;
}

// Handle user memory faults
int mm_fault(struct vm_desc *vma, uintptr_t addr, unsigned long flags)
{
    uintptr_t page_start = PAGE_ROUND_DOWN(addr);

    if (!check_access(vma, flags))
        return FAULT_PROT_VIOLATION;

    if (flags & FAULT_PTE_EXIST) {
        if (!(flags & FAULT_WRITE) || !(vma->vm_flags & VM_WRITE))
            return FAULT_PROT_VIOLATION;
        return do_cow_fault(vma, page_start);
    }

    if (vma->vm_file)
        return do_file_fault(vma, page_start);
    else
        return do_anon_fault(vma, page_start);
}
