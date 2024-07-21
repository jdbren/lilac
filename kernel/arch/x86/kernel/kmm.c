// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <mm/kmm.h>

#include <string.h>
#include <stdbool.h>

#include <lilac/panic.h>
#include <utility/multiboot2.h>
#include <utility/efi.h>

#include "pgframe.h"
#include "paging.h"


#define KHEAP_START_ADDR    0xC0400000
#define KHEAP_MAX_ADDR      0xEFFFF000
#define check_bit(var,pos) ((var) & (1<<(pos)))
#define get_index(page) ((u32)(page - KHEAP_START_ADDR)/ (32 * PAGE_SIZE))
#define get_offset(page) (((u32)(page - KHEAP_START_ADDR) / PAGE_SIZE) % 32)

typedef struct memory_desc memory_desc_t;

static void *__check_bitmap(int i, int num_pages, int *count, int *start);
static void __free_page(u8 *page);
static void __parse_mmap(struct multiboot_tag_efi_mmap *mmap);

extern const int _kernel_end;
extern const int _kernel_start;

#define KHEAP_PAGES ((KHEAP_MAX_ADDR - KHEAP_START_ADDR) / PAGE_SIZE)
#define KHEAP_BITMAP_SIZE (KHEAP_PAGES / 8)
#define HEAP_MANAGE_BYTES PAGE_ROUND_UP(KHEAP_BITMAP_SIZE)

static u32 kheap_bitmap[HEAP_MANAGE_BYTES / 4];

// TODO: Add support for greater than 4GB memory
void mm_init(struct multiboot_tag_efi_mmap *mmap)
{
    int phys_map_sz = phys_mem_init(mmap);
    __parse_mmap(mmap);

    kstatus(STATUS_OK, "Kernel virtual address allocation enabled\n");
}

static void *find_vaddr(int num_pages)
{
    if (num_pages > 0x0001000)
        kerror("Cannot allocate more than 4MB kernel mem at a time");
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
    to_map = (void*)((u32)to_map & 0xFFFFF000);
    void *virt = find_vaddr(num_pages);
    map_pages(to_map, virt, flags, num_pages);
    return virt;
}

void unmap_phys(void *addr, int size)
{
    int num_pages = PAGE_ROUND_UP(size) / PAGE_SIZE;
    addr = (void*)((u32)addr & 0xFFFFF000);
    free_vaddr(addr, num_pages);
    unmap_pages(addr, num_pages);
}

u32 virt_to_phys(void *vaddr)
{
    return (u32)get_physaddr(vaddr);
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
    void *vaddr = NULL;
    for (u32 i = 0; i < mmap->size; i += mmap->descr_size,
            entry = (efi_memory_desc_t*)((u32)entry + mmap->descr_size)) {
        klog(LOG_DEBUG, "Type: %d\n", entry->type);
        klog(LOG_DEBUG, "Phys addr: %x\n", entry->phys_addr);
        klog(LOG_DEBUG, "Num pages: %d\n", entry->num_pages);
        switch (entry->type) {
            case EFI_BOOT_SERVICES_CODE:
            case EFI_BOOT_SERVICES_DATA:
                if (entry->phys_addr < (u32)&_kernel_start)
                    continue;
                else
                    free_frames((void*)entry->phys_addr, entry->num_pages);
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
