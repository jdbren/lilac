#ifndef MM_PMEM_H
#define MM_PMEM_H

#include <lilac/lilac.h>
#include <stdatomic.h>

static __always_inline
unsigned long ___phys_addr(unsigned long x)
{
#ifdef __x86_64__
	unsigned long y = x - __KERNEL_BASE;

	/* use the carry flag to determine if x was < __KERNEL_BASE */
	return y + ((x > y) ? 0UL : (__KERNEL_BASE - __PHYS_MAP_ADDR));
#else
    return x >= __PHYS_MAP_ADDR ? x - __PHYS_MAP_ADDR : __pa(x);
#endif
}

#define __phys_addr(x) ___phys_addr((unsigned long)(x))

#define phys_to_pfn(phys) ((unsigned long)((phys) >> PAGE_SHIFT))
#define pfn_to_phys(pfn)  ((uintptr_t)(pfn) << PAGE_SHIFT)

#define page_to_pfn(frame) ((unsigned long)((frame) - phys_frames))
#define pfn_to_page(pfn) (phys_frames + (pfn))

#define page_to_phys(page)	pfn_to_phys(page_to_pfn(page))
#define phys_to_page(phys)	pfn_to_page(phys_to_pfn(phys))

#define virt_to_page(kaddr) pfn_to_page(__phys_addr(kaddr) >> PAGE_SHIFT)

#define virt_to_phys(kaddr) __phys_addr(kaddr)


#define ALLOC_NORMAL    0x0
#define ALLOC_DMA       0x1


struct page {
    unsigned int flags;
    atomic_uint refcount;
    struct list_head cache;
    void *virt;
};

extern struct page *phys_frames;
extern u8 *const phys_mem_mapping;

void pmem_init(void);
void * arch_map_frame_bitmap(size_t size);

void *alloc_frames(u32 num_pages);
void free_frames(void *frame, u32 num_pages);

static inline void *alloc_frame(void)
{
    return alloc_frames(1);
}

static inline void free_frame(void *frame)
{
    free_frames(frame, 1);
}

static inline
void * get_page_addr(struct page *frame)
{
    return (void*)(phys_mem_mapping + page_to_phys(frame));
}

struct page * alloc_pages(u32 pgcnt, u32 flags);
void __free_pages(struct page *frame, u32 pgcnt);
void free_pages(void *addr, u32 pgcnt);
void * get_free_pages(u32 pgcnt, u32 flags);
void * get_zeroed_pages(u32 pgcnt, u32 flags);

static inline __always_inline
struct page * alloc_page(u32 flags)
{
    return alloc_pages(1, flags);
}

static inline __always_inline
void free_page(void *addr)
{
    free_pages(addr, 1);
}

static inline __always_inline
void __free_page(struct page *frame)
{
    __free_pages(frame, 1);
}

static inline __always_inline
void * get_free_page(u32 type)
{
    return get_free_pages(1, type);
}

static inline
void * get_page(struct page *pg)
{
    pg->refcount++;
    return get_page_addr(pg);
}

static inline
void put_page(struct page *pg)
{
    if (atomic_fetch_sub(&pg->refcount, 1) == 1) {
        __free_page(pg);
    }
}

#endif
