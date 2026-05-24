#ifndef _LILAC_MM_H_
#define _LILAC_MM_H_

#include <lilac/types.h>
#include <lilac/rwsem.h>
#include <lilac/mman-bits.h>

struct tlb_inval;

struct mm_info {
    struct vm_desc *mmap;
    // struct rb_root mmap_rb;
    uintptr_t pgd;
    atomic_uint ref_count;
    // u32 map_count;
    struct rw_semaphore mmap_lock;
    spinlock_t page_table_lock;
    // struct list_head mmlist;
    uintptr_t start_code, end_code;
    uintptr_t start_data, end_data;
    uintptr_t start_brk, brk;
    uintptr_t start_stack;
    uintptr_t arg_start, arg_end;
    uintptr_t env_start, env_end;
    size_t total_vm;
};

struct mm_info * alloc_mm_info(void);

#define mmap_read_lock(mm) down_read(&(mm)->mmap_lock)
#define mmap_write_lock(mm) down_write(&(mm)->mmap_lock)
#define mmap_read_unlock(mm) up_read(&(mm)->mmap_lock)
#define mmap_write_unlock(mm) up_write(&(mm)->mmap_lock)
#define mmap_drop_to_read(mm) downgrade_write(&(mm)->mmap_lock)


#define VM_NONE         0x0000
#define VM_READ         0x0001
#define VM_WRITE        0x0002
#define VM_EXEC         0x0004
#define VM_SHARED       0x0008
#define VM_IO           0x0010
#define VM_PFNMAP       0x0020

struct vm_desc {
    uintptr_t start;
    uintptr_t end;
    struct mm_info *mm;

    /* list sorted by address */
    struct vm_desc *vm_next, *vm_prev;
    // rb_node_t vm_rb;

    int vm_prot;
    int vm_flags;

    unsigned int vm_pgoff; // offset in pages from start of backing file
    unsigned int vm_fsize; // size in file
    struct file *vm_file;

    uintptr_t seg_vaddr;   // EXACT p_vaddr
    size_t    seg_offset;  // EXACT p_offset
};

struct vm_desc * find_vma(struct mm_info *mm, uintptr_t addr);

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
    FAULT_OOM,
};

int mm_fault(struct vm_desc *vma, uintptr_t addr, unsigned long flags);

void drop_user_page_range(uintptr_t start, size_t size);

#endif // _LILAC_MM_H_
