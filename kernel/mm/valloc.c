// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <mm/kmm.h>
#include <lilac/lilac.h>
#include <lilac/boot.h>
#include <mm/page.h>
#include <utility/multiboot2.h>
#include <utility/efi.h>

// #define DEBUG_KMM 1

#define check_bit(var,pos) ((var) & (1<<(pos)))
#define get_index(page) ((size_t)(page - KHEAP_START_ADDR)/ (32 * PAGE_SIZE))
#define get_offset(page) (((size_t)(page - KHEAP_START_ADDR) / PAGE_SIZE) % 32)

static void *__check_bitmap(int i, int num_pages, int *count, int *start);
static void __free_page(u8 *page);

#define KHEAP_PAGES ((KHEAP_MAX_ADDR - KHEAP_START_ADDR) / PAGE_SIZE)
#define KHEAP_BITMAP_SIZE (KHEAP_PAGES / 8)
#define HEAP_MANAGE_BYTES PAGE_ROUND_UP(KHEAP_BITMAP_SIZE)

static uintptr_t kheap_bitmap[HEAP_MANAGE_BYTES / 4];

static void *find_vaddr(int num_pages)
{
    // if (num_pages > 0x200)
    //     kerror("Cannot allocate more than 2MB kernel mem at a time");
    if (num_pages <= 0)
        return NULL;

    void *ptr = NULL;
    int start = 0;
    int count = 0;
    for (size_t i = 0; i < KHEAP_BITMAP_SIZE / sizeof(u32); i++) {
        if (kheap_bitmap[i] != ~0UL) {
            ptr = __check_bitmap(i, num_pages, &count, &start);
            if (ptr)
                break;
        }
        else
            count = 0;
    }

    if (!ptr)
        kerror("KERNEL OUT OF VIRTUAL MEMORY");

#ifdef DEBUG_KMM
    klog(LOG_DEBUG, "Allocated %d pages at %x\n", num_pages, ptr);
#endif
    return ptr;
}

static void free_vaddr(u8 *page, u32 num_pages)
{
#ifdef DEBUG_KMM
    klog(LOG_DEBUG, "Freed %d pages at %x\n", num_pages, page);
#endif
    for (u8 *end = page + num_pages * PAGE_SIZE; page < end; page += PAGE_SIZE)
        __free_page(page);
}

void * get_free_vaddr(int num_pages)
{
    return find_vaddr(num_pages);
}

int map_to_self(void *addr, int size, int flags)
{
    return map_pages(addr, addr, flags, PAGE_ROUND_UP(size) / PAGE_SIZE);
}

void unmap_from_self(void *addr, int size)
{
    unmap_pages(addr, PAGE_ROUND_UP(size) / PAGE_SIZE);
}

void *map_phys(void *to_map, int size, int flags)
{
    int num_pages = PAGE_ROUND_UP(size) / PAGE_SIZE;
    to_map = (void*)((uintptr_t)to_map & ~0xFFFul);
    void *virt = find_vaddr(num_pages);
    map_pages(to_map, virt, flags, num_pages);
    return virt;
}

void unmap_phys(void *addr, int size)
{
    int num_pages = PAGE_ROUND_UP(size) / PAGE_SIZE;
    addr = (void*)((uintptr_t)addr & ~0xFFFul);
    unmap_pages(addr, num_pages);
    free_vaddr(addr, num_pages);
}

void * map_virt(void *virt, int size, int flags)
{
    int num_pages = PAGE_ROUND_UP(size) / PAGE_SIZE;
    void *phys = alloc_frames(num_pages);
    map_pages(phys, virt, flags, num_pages);
    return phys;
}

void unmap_virt(void *virt, int size)
{
    int num_pages = PAGE_ROUND_UP(size) / PAGE_SIZE;
    free_frames((void*)__walk_pages(virt), num_pages);
    unmap_pages(virt, num_pages);
}



static void* __do_page_alloc(int start, int num_pages)
{
    for (int k = start; k < start + num_pages; k++) {
        int index = k / 32;
        int offset = k % 32;

        kheap_bitmap[index] |= (1 << offset);
    }

    return (void*)(KHEAP_START_ADDR + start * PAGE_SIZE);
}

static void* __check_bitmap(int i, int num_pages, int *count, int *start)
{
    for (int j = 0; j < 32; j++) {
        if (!check_bit(kheap_bitmap[i], j)) {
            if (*count == 0)
                *start = i * 32 + j;

            (*count)++;

            if (*count == num_pages)
                return __do_page_alloc(*start, num_pages);
        }
        else {
            *count = 0;
        }
    }

    return 0;
}

static void __free_page(u8 *page)
{
    u32 index = get_index(page);
    u32 offset = get_offset(page);

    kheap_bitmap[index] &= ~(1 << offset);
}
