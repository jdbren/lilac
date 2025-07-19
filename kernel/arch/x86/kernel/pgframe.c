// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/panic.h>
#include <lilac/config.h>
#include <lilac/libc.h>
#include <utility/efi.h>
#include "pgframe.h"
#include "paging.h"


#define get_phys_addr(virt_addr) ((uintptr_t)(virt_addr) - __KERNEL_BASE)
#define check_bit(var,pos) ((var) & (1<<(pos)))
#define get_index(frame) ((uintptr_t)frame / (32 * PAGE_SIZE))
#define get_offset(frame) (((uintptr_t)frame / PAGE_SIZE) % 32)

typedef struct __packed page {
    u8 pg[PAGE_SIZE];
} page_t;

extern const u32 _kernel_start;
extern const u32 _kernel_end;
static uintptr_t FIRST_PAGE;
static u32 BITMAP_SIZE;
static volatile u32 *pg_frame_bitmap;

static void __init_bitmap(struct multiboot_tag_efi_mmap *mmap);
static void *__check_bitmap(int i, int num_pages, int *count, int *start);
static void *__do_frame_alloc(int, int);
static void __free_frame(page_t *frame);

// bit #i in byte #n define the status of page #n*8+i
// 0 = free, 1 = used

int phys_mem_init(struct multiboot_tag_efi_mmap *mmap)
{
    u32 phys_map_pgcnt = 0;
    efi_memory_desc_t *entry = (efi_memory_desc_t*)mmap->efi_mmap;
    for (u32 i = 0; i < mmap->size; i += mmap->descr_size) {
        phys_map_pgcnt += entry->num_pages;
        entry = (efi_memory_desc_t*)((uintptr_t)entry + mmap->descr_size);
    }

    BITMAP_SIZE = phys_map_pgcnt / 8 + 8; // in bytes
    FIRST_PAGE = 0;

#ifdef __x86_64__
    pg_frame_bitmap = (void*)(((uintptr_t)(&_kernel_end) & ~0x1fffffUL) + 0x200000);
    __map_frame_bm((void*)get_phys_addr(pg_frame_bitmap),
            (void*)pg_frame_bitmap);
#else
    assert((uintptr_t)&_kernel_end + BITMAP_SIZE < __KERNEL_MAX_ADDR);
    pg_frame_bitmap = (u32*)&_kernel_end;
    map_pages((void*)get_phys_addr(pg_frame_bitmap),
            (void*)pg_frame_bitmap,
            PG_WRITE,
            BITMAP_SIZE / PAGE_SIZE + 1);
#endif
    memset((void*)pg_frame_bitmap, 0, BITMAP_SIZE);

    __init_bitmap(mmap);

    return (BITMAP_SIZE & 0xfffff000) + PAGE_SIZE;
}

void* alloc_frames(u32 num_pages)
{
    if (num_pages == 0)
        return 0;

    int start = 0;
    int count = 0;
    for (size_t i = 0; i < BITMAP_SIZE / sizeof(u32); i++) {
        if (pg_frame_bitmap[i] != ~0U) {
            void *ptr = __check_bitmap(i, num_pages, &count, &start);
            if (ptr) {
#ifdef DEBUG_PAGING
                klog(LOG_DEBUG, "Allocated %d physical frames at %p\n", num_pages, ptr);
#endif
                return ptr;
            }
        }
        else
            count = 0;
    }

    kerror("Out of memory");
    return 0;
}

void free_frames(void *frame, u32 num_pages)
{
    if (num_pages == 0)
        return;

    for (page_t *pg = (page_t*)frame, *end = (page_t*)frame + num_pages;
    pg < end; pg++) {
        __free_frame(pg);
    }
#ifdef DEBUG_PAGING
    klog(LOG_DEBUG, "Freed %d physical frames at %p\n", num_pages, frame);
#endif
}

static void __mark_frames(size_t index, size_t offset, size_t pg_cnt)
{
    while (offset < 32 && pg_cnt > 0) {
        pg_frame_bitmap[index] |= (1 << offset);
        offset++;
        pg_cnt--;
    }
    index++;
    while (pg_cnt >= 32) {
        pg_frame_bitmap[index++] = ~0U;
        pg_cnt -= 32;
    }
    offset = 0;
    while (pg_cnt > 0) {
        pg_frame_bitmap[index] |= (1 << offset++);
        pg_cnt--;
    }
}

static void __init_bitmap(struct multiboot_tag_efi_mmap *mmap)
{
    __mark_frames(get_index(0x0),
        get_offset(0x0),
        (get_phys_addr(pg_frame_bitmap) + BITMAP_SIZE) / PAGE_SIZE + 1);
    efi_memory_desc_t *entry = (efi_memory_desc_t*)mmap->efi_mmap;
    for (u32 i = 0; i < mmap->size; i += mmap->descr_size) {
        if (entry->type != EFI_CONVENTIONAL_MEMORY && entry->type != EFI_RESERVED_TYPE) {
            __mark_frames(get_index(entry->phys_addr),
                    get_offset(entry->phys_addr),
                    entry->num_pages);
        }
        entry = (efi_memory_desc_t*)((uintptr_t)entry + mmap->descr_size);
    }
}

static void* __check_bitmap(int i, int num_pages, int *count, int *start)
{
    for (int j = 0; j < 32; j++) {
        if (!check_bit(pg_frame_bitmap[i], j)) {
            if (*count == 0)
                *start = i * 32 + j;

            (*count)++;

            if (*count == num_pages)
                return __do_frame_alloc(*start, num_pages);
        }
        else {
            *count = 0;
        }
    }

    return 0;
}

static void* __do_frame_alloc(int start, int num_pages)
{
    for (int k = start; k < start + num_pages; k++) {
        int index = k / 32;
        int offset = k % 32;

        pg_frame_bitmap[index] |= (1 << offset);
    }

    return (void*)(FIRST_PAGE + start * PAGE_SIZE);
}

static void __free_frame(page_t *frame)
{
    u32 index = get_index(frame);
    u32 offset = get_offset(frame);

    pg_frame_bitmap[index] &= ~(1 << offset);
}

void print_bitmap(void)
{
    for (size_t i = 0; i < BITMAP_SIZE / sizeof(u32); i++) {
        for (int j = 0; j < 32; j++) {
            if (check_bit(pg_frame_bitmap[i], j))
                putchar('1');
            else
                putchar('0');
        }
        putchar('\n');
    }
}

int num_free_frames(void)
{
    int count = 0;
    for (size_t i = 0; i < BITMAP_SIZE / sizeof(u32); i++) {
        for (int j = 0; j < 32; j++) {
            if (!check_bit(pg_frame_bitmap[i], j))
                count++;
        }
    }
    return count;
}

int num_used_frames(void)
{
    int count = 0;
    for (size_t i = 0; i < BITMAP_SIZE / sizeof(u32); i++) {
        for (int j = 0; j < 32; j++) {
            if (check_bit(pg_frame_bitmap[i], j))
                count++;
        }
    }
    return count;
}
