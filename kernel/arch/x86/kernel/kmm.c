// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <mm/kmm.h>
#include <string.h>
#include <stdbool.h>
#include <utility/multiboot2.h>
#include <kernel/panic.h>
#include <utility/efi.h>
#include "pgframe.h"
#include "paging.h"


#define KHEAP_START_ADDR    0xC8000000
#define KHEAP_MAX_ADDR      0xEFFFF000
#define get_index(VIRT_ADDR) ((KHEAP_MAX_ADDR - VIRT_ADDR) / PAGE_SIZE)

typedef struct memory_desc memory_desc_t;

struct memory_desc {
    memory_desc_t *next, *prev;
    u32 size;
    u8 *start;
};

static void __update_list(memory_desc_t *mem_addr, unsigned int num_pages);
static void __parse_mmap(struct multiboot_tag_efi_mmap *mmap);

extern u32 _kernel_end;
extern u32 _kernel_start;

static memory_desc_t *avail_vmem_list;
static memory_desc_t *list;
static u32 unused_heap_addr;

static const int KHEAP_PAGES = (KHEAP_MAX_ADDR - KHEAP_START_ADDR) / PAGE_SIZE;
static const int HEAP_MANAGE_PAGES = KHEAP_PAGES * sizeof(struct memory_desc) / PAGE_SIZE;

// TODO: Add support for greater than 4GB memory
void mm_init(struct multiboot_tag_efi_mmap *mmap)
{
    int phys_map_sz = phys_mem_init(mmap);
    avail_vmem_list = (memory_desc_t*)((u32)&_kernel_end + (u32)phys_map_sz);
    list = 0;
    unused_heap_addr = KHEAP_MAX_ADDR;

    if ((u32)avail_vmem_list + HEAP_MANAGE_PAGES * PAGE_SIZE > __KERNEL_MAX_ADDR)
        kerror("Heap management pages exceed kernel space");

    void *phys = alloc_frames(HEAP_MANAGE_PAGES);
    map_pages(phys, (void*)avail_vmem_list, PG_WRITE, HEAP_MANAGE_PAGES);
    memset(avail_vmem_list, 0, HEAP_MANAGE_PAGES * PAGE_SIZE);

    __parse_mmap(mmap);

    printf("Kernel virtual address allocation enabled\n");
}

static void *find_vaddr(int num_pages)
{
    void *ptr = NULL;
    memory_desc_t *mem_addr = list;
    while (mem_addr) {
        // printf("mem_addr: %x\n", mem_addr->start);
        // printf("mem_addr->size: %x\n", mem_addr->size);
        if (mem_addr->size >= num_pages) {
            ptr = mem_addr->start;
            __update_list(mem_addr, num_pages);
            return ptr;
        }
        mem_addr = mem_addr->next;
    }

    if (unused_heap_addr >= KHEAP_START_ADDR)
        ptr = (u8*)unused_heap_addr - (num_pages - 1) * PAGE_SIZE;
    else
        kerror("KERNEL OUT OF VIRTUAL MEMORY");

    unused_heap_addr = (u32)unused_heap_addr - num_pages * PAGE_SIZE;
    return ptr;
}

void* kvirtual_alloc(int size, int flags)
{
    int done = 0;
    u32 num_pages = PAGE_ROUND_UP(size) / PAGE_SIZE;
    void *ptr = NULL;

    ptr = find_vaddr(num_pages);

    if (ptr) {
        void *phys = alloc_frames(num_pages);
        map_pages(phys, ptr, flags, num_pages);
        memset(ptr, 0, num_pages * PAGE_SIZE);
    }

    return ptr;
}

static void free_vaddr(void *addr, int num_pages)
{
    memory_desc_t *mem_addr = &avail_vmem_list[get_index((u32)addr)];
    //printf("mem_addr: %x\n", mem_addr);
    mem_addr->start = (u8*)addr;
    mem_addr->size = num_pages;

    if (list) {
        list->prev = mem_addr;
        mem_addr->next = list;
    }

    list = mem_addr;
}

void kvirtual_free(void* addr, int size)
{
    int num_pages = PAGE_ROUND_UP(size) / PAGE_SIZE;
    free_vaddr(addr, size);
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

static void __update_list(memory_desc_t *mem_addr, unsigned int num_pages)
{
    void *ptr = (void*)(mem_addr->start);

    if (mem_addr->size > num_pages) {
        u32 tmp = (u32)mem_addr->start + num_pages * PAGE_SIZE;

        memory_desc_t *new_desc = &avail_vmem_list[get_index(tmp)];
        new_desc->start = (u8*)tmp;
        new_desc->size = mem_addr->size - num_pages;
        new_desc->prev = mem_addr->prev;
        new_desc->next = mem_addr->next;

        if (mem_addr->prev)
            mem_addr->prev->next = new_desc;
        if (mem_addr->next)
            mem_addr->next->prev = new_desc;
        mem_addr->next = new_desc;
    } else {
        if (mem_addr->prev)
            mem_addr->prev->next = mem_addr->next;
        if (mem_addr->next)
            mem_addr->next->prev = mem_addr->prev;
    }

    if (list == mem_addr)
        list = mem_addr->next;

    memset(mem_addr, 0, sizeof(*mem_addr));
}

static void __parse_mmap(struct multiboot_tag_efi_mmap *mmap)
{
    efi_memory_desc_t *entry = (efi_memory_desc_t*)mmap->efi_mmap;
    void *vaddr = NULL;
    for (u32 i = 0; i < mmap->size; i += mmap->descr_size,
            entry = (efi_memory_desc_t*)((u32)entry + mmap->descr_size)) {
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
