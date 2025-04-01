// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <mm/kmm.h>
#include <lilac/lilac.h>
#include <utility/multiboot2.h>
#include <utility/efi.h>

#include "pgframe.h"
#include "paging.h"

// #define DEBUG_KMM 1

#define check_bit(var,pos) ((var) & (1<<(pos)))
#define get_index(page) ((size_t)(page - KHEAP_START_ADDR)/ (32 * PAGE_SIZE))
#define get_offset(page) (((size_t)(page - KHEAP_START_ADDR) / PAGE_SIZE) % 32)

typedef struct memory_desc memory_desc_t;

static void *__check_bitmap(int i, int num_pages, int *count, int *start);
static void __free_page(u8 *page);
static void __parse_mmap(struct multiboot_tag_efi_mmap *mmap);

extern const uintptr_t _kernel_end;
extern const uintptr_t _kernel_start;

#define KHEAP_PAGES ((KHEAP_MAX_ADDR - KHEAP_START_ADDR) / PAGE_SIZE)
#define KHEAP_BITMAP_SIZE (KHEAP_PAGES / 8)
#define HEAP_MANAGE_BYTES PAGE_ROUND_UP(KHEAP_BITMAP_SIZE)

static uintptr_t kheap_bitmap[HEAP_MANAGE_BYTES / 4];
size_t memory_size_kb;

static void __set_memory_size(struct multiboot_tag_efi_mmap *mmap)
{
    efi_memory_desc_t *entry = (efi_memory_desc_t*)mmap->efi_mmap;
    for (u32 i = 0; i < mmap->size; i += mmap->descr_size,
        entry = (efi_memory_desc_t*)((uintptr_t)entry + mmap->descr_size)) {
        if (entry->type != EFI_RESERVED_TYPE)
            memory_size_kb += entry->num_pages * PAGE_SIZE / 1024;
    }
}

// TODO: Add support for greater than 4GB memory
void mm_init(struct multiboot_tag_efi_mmap *mmap)
{
    __set_memory_size(mmap);
    init_phys_mem_mapping(memory_size_kb);
    phys_mem_init(mmap);
    __parse_mmap(mmap);

    // Allocate page tables for all kernel space
    kernel_pt_init(KHEAP_START_ADDR, KHEAP_MAX_ADDR);
}

size_t arch_get_mem_sz(void)
{
    return memory_size_kb;
}

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

void* kvirtual_alloc(int size, int flags)
{
    if (size == 0)
        return NULL;

    u32 num_pages = PAGE_ROUND_UP(size) / PAGE_SIZE;
    void *ptr = find_vaddr(num_pages);

    if (ptr) {
        void *phys = alloc_frames(num_pages);
        if (!phys) {
            kerror("OUT OF PHYSICAL MEMORY");
        }
        map_pages(phys, ptr, flags, num_pages);
        memset(ptr, 0, num_pages * PAGE_SIZE);
    }

    return ptr;
}

void kvirtual_free(void* addr, int size)
{
    int num_pages = PAGE_ROUND_UP(size) / PAGE_SIZE;
    free_vaddr(addr, num_pages);
    free_frames(get_physaddr(addr), num_pages);
    unmap_pages(addr, num_pages);
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
    to_map = (void*)((uintptr_t)to_map & ~0xFFF);
    void *virt = find_vaddr(num_pages);
    map_pages(to_map, virt, flags, num_pages);
    return virt;
}

void *map_phys_at(void *phys, void *virt, int size, int flags)
{
    int num_pages = PAGE_ROUND_UP(size) / PAGE_SIZE;
    map_pages(phys, virt, flags, num_pages);
    return virt;
}

void unmap_phys(void *addr, int size)
{
    int num_pages = PAGE_ROUND_UP(size) / PAGE_SIZE;
    addr = (void*)((uintptr_t)addr & ~0xFFF);
    free_vaddr(addr, num_pages);
    unmap_pages(addr, num_pages);
}

void *map_virt(void *virt, int size, int flags)
{
    int num_pages = PAGE_ROUND_UP(size) / PAGE_SIZE;
    void *phys = alloc_frames(num_pages);
    map_pages(phys, virt, flags, num_pages);
    return phys;
}

void unmap_virt(void *virt, int size)
{
    int num_pages = PAGE_ROUND_UP(size) / PAGE_SIZE;
    free_frames(get_physaddr(virt), num_pages);
    unmap_pages(virt, num_pages);
}

uintptr_t virt_to_phys(void *vaddr)
{
    return (uintptr_t)get_physaddr(vaddr);
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

static void __parse_mmap(struct multiboot_tag_efi_mmap *mmap)
{
    efi_memory_desc_t *entry = (efi_memory_desc_t*)mmap->efi_mmap;
    //void *vaddr = NULL;
    for (u32 i = 0; i < mmap->size; i += mmap->descr_size,
            entry = (efi_memory_desc_t*)((uintptr_t)entry + mmap->descr_size)) {
        switch (entry->type) {
            case EFI_BOOT_SERVICES_CODE:
            case EFI_BOOT_SERVICES_DATA:
                if (entry->phys_addr < (uintptr_t)&_kernel_start)
                    continue;
                else
                    free_frames((void*)((uintptr_t)entry->phys_addr), entry->num_pages);
            break;
            case EFI_RUNTIME_SERVICES_DATA:
                // vaddr = find_vaddr(entry->num_pages);
                // map_pages((void*)entry->phys_addr, vaddr, PG_WRITE, entry->num_pages);
                // entry->virt_addr = (u32)vaddr;
                // map_pages((void*)entry->phys_addr, (void*)entry->phys_addr, PG_WRITE, entry->num_pages);

            break;
            case EFI_RUNTIME_SERVICES_CODE:
                // vaddr = find_vaddr(entry->num_pages);
                // map_pages((void*)entry->phys_addr, vaddr, 0, entry->num_pages);
                // entry->virt_addr = (u32)vaddr;
                // map_pages((void*)entry->phys_addr, (void*)entry->phys_addr, 0, entry->num_pages);
            break;
        }
    }
}

int umem_alloc(uintptr_t vaddr, int num_pages)
{
    void *frames = alloc_frames(num_pages);
    if (!frames) {
        kerror("OUT OF PHYSICAL MEMORY");
        return -ENOMEM;
    }
    if (map_pages(frames, (void*)vaddr, PG_USER | PG_WRITE, num_pages) != 0) {
        kerror("Failed to map pages");
        free_frames(frames, num_pages);
        return -ENOMEM;
    }
    return 0;
}
