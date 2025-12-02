#include <mm/mm.h>
#include <mm/kmm.h>
#include <mm/page.h>
#include <lilac/panic.h>
#include <lilac/fs.h>

static bool check_access(struct vm_desc *vma, unsigned int flags)
{
    if (flags & FAULT_WRITE && !(vma->vm_flags & VM_WRITE)) {
        return false;
    }
    if (flags & FAULT_INSTR && !(vma->vm_flags & VM_EXEC)) {
        return false;
    }
    return true;
}

static inline void * fault_page_alloc(void)
{
    void *page = get_free_page();
    if (!page)
        panic("Out of memory allocating page for page fault\n");
    return page;
}

static int do_file_fault(struct vm_desc *vma, uintptr_t pgaddr, unsigned long flags)
{
    struct file *f = vma->vm_file;
    uintptr_t seg_vaddr  = vma->seg_vaddr;   /* exact ELF p_vaddr */
    size_t    seg_offset = vma->seg_offset;  /* exact ELF p_offset */
    size_t    fsize      = vma->vm_fsize;    /* number of bytes in file for this segment */

    u8 *buf = fault_page_alloc();
    if (!buf)
        return FAULT_OOM;

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

    vfs_lseek(f, file_offset, 0);
    ssize_t bytes = vfs_read(f, buf + start_in_page, bytes_to_read);
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
    {
        int mem_pflags = MEM_PF_USER;
        if (vma->vm_flags & VM_WRITE)
            mem_pflags |= MEM_PF_WRITE;

        map_page((void *)virt_to_phys(buf), (void *)pgaddr, mem_pflags);
    }

    return FAULT_SUCCESS;
}

static int do_anon_fault(struct vm_desc *vma, uintptr_t pgaddr, unsigned long flags)
{
    void *page = get_zeroed_pages(1, ALLOC_NORMAL);
    int mem_pflags = MEM_PF_USER;
    if (vma->vm_flags & VM_WRITE)
        mem_pflags |= MEM_PF_WRITE;
    map_page((void*)virt_to_phys(page), (void*)pgaddr, mem_pflags);
    return FAULT_SUCCESS;
}

// Handle user memory faults
int mm_fault(struct vm_desc *vma, uintptr_t addr, unsigned long flags)
{
    uintptr_t page_start = PAGE_ROUND_DOWN(addr);

    if (!check_access(vma, flags))
        return FAULT_PROT_VIOLATION;

    if (flags & FAULT_PTE_EXIST) {
        // TODO: Handle present PTE case (copy-on-write)
        kerror("Page table entry already exists for address %lx\n", addr);
    }

    if (vma->vm_file)
        return do_file_fault(vma, page_start, flags);
    else
        return do_anon_fault(vma, page_start, flags);
}
