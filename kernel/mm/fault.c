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
    void *page = get_zeroed_pages(1, ALLOC_NORMAL);
    if (!page)
        panic("Out of memory allocating page for page fault\n");
    return page;
}

static int do_file_fault(struct vm_desc *vma, uintptr_t pgaddr, unsigned long flags)
{
    struct file *f = vma->vm_file;
    size_t file_offset = vma->vm_pgoff * PAGE_SIZE + (pgaddr - vma->start);
    u8* buf = fault_page_alloc();

    // If end of file is within this page, only read that much
    size_t to_read = PAGE_SIZE;
    klog(LOG_DEBUG, "File size: %u, File offset: %lx\n", vma->vm_fsize, file_offset);
    if (file_offset + PAGE_SIZE > vma->vm_pgoff * PAGE_SIZE + vma->vm_fsize) {
        to_read = vma->vm_pgoff * PAGE_SIZE + vma->vm_fsize - file_offset;
    }

    klog(LOG_DEBUG, "Handling file-backed page fault at %lx, file offset %lx, bytes %ld\n", pgaddr, file_offset, to_read);

    vfs_lseek(f, file_offset, 0);
    ssize_t bytes = vfs_read(f, buf, to_read);
    if (bytes < 0) {
        free_page(buf);
        return FAULT_FILE_ERROR;
    }

    if (bytes < PAGE_SIZE) {
        klog(LOG_DEBUG, "Partial page read: %ld bytes, zeroing rest\n", bytes);
        memset(buf + bytes, 0, PAGE_SIZE - bytes);
    }

    int mem_pflags = MEM_PF_USER;
    if (vma->vm_flags & VM_WRITE)
        mem_pflags |= MEM_PF_WRITE;
    map_page((void*)virt_to_phys(buf), (void*)pgaddr, mem_pflags);

    return FAULT_SUCCESS;
}

static int do_anon_fault(struct vm_desc *vma, uintptr_t pgaddr, unsigned long flags)
{
    void *page = fault_page_alloc();
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

    // if (!(flags & FAULT_USER))
    //     panic("Kernel mode access violation at %lx\n", addr);

    if (!check_access(vma, flags))
        return FAULT_PROT_VIOLATION;

    if (flags & FAULT_PTE_EXIST) {
        // TODO: Handle present PTE case (copy-on-write)
        kerror("Page table entry already exists for address %lx\n", addr);
    }

    klog(LOG_DEBUG, "Handling user page fault for address %lx\n", addr);

    if (vma->vm_file && page_start < vma->start + vma->vm_fsize)
        return do_file_fault(vma, page_start, flags);
    else
        return do_anon_fault(vma, page_start, flags);
}
