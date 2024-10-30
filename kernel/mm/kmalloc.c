// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#include <lilac/config.h>
#include <lilac/types.h>
#include <lilac/panic.h>
#include <mm/kmalloc.h>
#include <mm/kmm.h>

#define MIN_ALLOC 4
#define MIN_ALLOC_POWER 2
#define RETAIN_FREE_SUPERBLOCK_COUNT 10
#define PAGE_MASK (~(PAGE_SIZE - 1))
#define is_aligned(POINTER, BYTE_COUNT) \
    (((uintptr_t)(POINTER)) % (BYTE_COUNT) == 0)

#define SUPERBLOCKSIZE (PAGE_SIZE)
#define SUPERBLOCKPAGES (SUPERBLOCKSIZE / PAGE_SIZE)
#define BUCKETS 13

typedef union Alloc {
    void *next;
    void *data;
} alloc_t;

struct sb_list {
    struct sb_header *head;
    u32 alloc_size;
    u32 free_count;
    u32 num_sb;
};

struct sb_header {
    struct sb_header *next;
    struct sb_header *prev;
    alloc_t *free;
    union {
        u16 alloc_size;
        u16 num_pages;
    };
    u16 free_count;
    bool is_large;
} __align(32);


static struct sb_list buckets[BUCKETS];

static void *malloc_small(size_t);
static void *malloc_large(size_t);
static void init_super(struct sb_header*, size_t, int);
static struct sb_header* manage_empty_sb(struct sb_header*, int);
static void sb_mtf(struct sb_header*, int);
static void sb_atf(struct sb_header*, int);
static size_t next_pow_2(size_t);


void *kmalloc(size_t size)
{
    if (size <= 0) return NULL;
    assert(BUCKETS >= log2(SUPERBLOCKSIZE)-MIN_ALLOC_POWER);

    void *alloc = NULL;

    if (size > SUPERBLOCKSIZE/2)
        alloc = malloc_large(size);
    else
        alloc = malloc_small(size);

    assert(is_aligned(alloc, MIN_ALLOC));
    assert(alloc != NULL);

    return alloc;
}

void *kzmalloc(size_t size)
{
    void *ptr = kmalloc(size);
    if (ptr != NULL)
        memset(ptr, 0, size);
    return ptr;
}

void *krealloc(void *addr, size_t size)
{
    if (addr == NULL)
        return kmalloc(size);
    if (size == 0) {
        kfree(addr);
        return NULL;
    }

    // get superblock header using bitmask
    u32 base = (u32)addr & PAGE_MASK;
    struct sb_header *header = (struct sb_header*)base;
    assert(is_aligned(header, PAGE_SIZE));

    // check if large allocation
    if (header->is_large) {
        if (size <= header->num_pages * PAGE_SIZE)
            return addr;
        void *new = kmalloc(size);
        memcpy(new, addr, size);
        kfree(addr);
        return new;
    }

    // get bucket index and step size
    u32 step = header->alloc_size;

    // check if new size is in same bucket or smaller
    if (size <= step)
        return addr;

    // allocate new memory
    void *new = kmalloc(size);
    memcpy(new, addr, step);
    kfree(addr);
    return new;
}

void *kcalloc(size_t num, size_t size)
{
    return kzmalloc(num * size);
}

void kfree(void *ptr)
{
    if (ptr == NULL) return;

    // get superblock header using bitmask
    u32 base = (u32)ptr & PAGE_MASK;
    struct sb_header *header = (struct sb_header*)base;
    assert(is_aligned(header, PAGE_SIZE));

    // check if large allocation
    if (header->is_large) {
        kvirtual_free(header, header->num_pages * PAGE_SIZE);
        return;
    }

    // get bucket index and step size
    int step = header->alloc_size;
    int bucket_idx = log2(step) - MIN_ALLOC_POWER;

    // return alloc to free list
    alloc_t *alloc = (alloc_t*)ptr;
    alloc->data = header->free;
    header->free = alloc;
    header->free_count++;
    buckets[bucket_idx].free_count++;

    assert(header->free_count <= (SUPERBLOCKSIZE - sizeof(struct sb_header)) / step);

    // check if superblock is empty
    if (header->free_count == (SUPERBLOCKSIZE - sizeof(struct sb_header)) / step)
        header = manage_empty_sb(header, bucket_idx);
    // move superblock to front of list
    if (header != NULL && !(header == buckets[bucket_idx].head))
        sb_mtf(header, bucket_idx);
}

static void* malloc_small(size_t size)
{
    // round up to next power of 2
    size = size < MIN_ALLOC ? MIN_ALLOC : next_pow_2(size);
    assert(size <= SUPERBLOCKSIZE/2);

    int bucket_idx = log2(size) - MIN_ALLOC_POWER;
    alloc_t *alloc = NULL;
    struct sb_header *header = NULL;

    // check if there is any memory available
    if (buckets[bucket_idx].free_count == 0) {
        // allocate new superblock
        void *block = kvirtual_alloc(SUPERBLOCKSIZE, PG_WRITE);
        assert(block != NULL);

        header = (struct sb_header*)block;
        // initialize superblock stats and free list
        init_super(header, size, bucket_idx);

        // add superblock to front of list
        sb_atf(header, bucket_idx);
    }
    else {
        // use existing superblock
        header = (struct sb_header*)buckets[bucket_idx].head;
        while (header->free_count == 0)
            header = header->next;
        // move superblock to front of list if it has room
        // trying to improve locality, I think Hoard does something similar
        if (header->free_count > 1 && header != buckets[bucket_idx].head)
            sb_mtf(header, bucket_idx);
    }

    assert(header->free_count > 0);
    assert(buckets[bucket_idx].alloc_size == size);

    // get alloc from free list
    alloc = header->free;

    // update free list
    header->free = alloc->next;
    header->free_count--;
    buckets[bucket_idx].free_count--;

    assert(is_aligned(alloc, size));
    return (void*)alloc;
}

// allocate large memory
static void* malloc_large(size_t size)
{
    alloc_t *alloc = NULL;
    int pages = (size + sizeof(struct sb_header))/PAGE_SIZE + 1;
    size = pages * PAGE_SIZE;

    void *block = kvirtual_alloc(size, PG_WRITE);
    assert(block != NULL);

    // set up superblock header
    struct sb_header *header = (struct sb_header*)block;
    header->is_large = true;
    header->num_pages = pages;
    header->free = (alloc_t*)((u8*)header + sizeof(struct sb_header));
    alloc = header->free;

    return (void*)alloc;
}

// Initialize a superblock and its free list
static void init_super(struct sb_header *header, size_t size, int bucket_idx)
{
    // make superblock header
    if (size > sizeof(struct sb_header))
        header->free = (alloc_t*)((u8*)header + size);
    else
        header->free = (alloc_t*)(header + 1);
    header->alloc_size = size;
    header->free_count = (SUPERBLOCKSIZE - sizeof(struct sb_header)) / size;
    // update bucket info
    buckets[bucket_idx].free_count += header->free_count;
    buckets[bucket_idx].num_sb++;
    buckets[bucket_idx].alloc_size = size;

    // make free list
    alloc_t *current = header->free, *next = 0;
    for (int i = 1; i < header->free_count; i++) {
        next = (alloc_t*)((u8*)(current) + size);
        current->next = next;
        current = next;
    }
}

// Move superblock to front of list
static void sb_mtf(struct sb_header *header, int bucket_idx)
{
    if (header->prev != NULL)
        header->prev->next = header->next;
    if (header->next != NULL)
        header->next->prev = header->prev;
    header->next = buckets[bucket_idx].head;
    if (header->next != NULL)
        header->next->prev = header;
    header->prev = NULL;
    buckets[bucket_idx].head = header;
}

// Add superblock to front of list
static void sb_atf(struct sb_header *header, int bucket_idx)
{
    header->next = buckets[bucket_idx].head;
    if (header->next != NULL)
        header->next->prev = header;
    header->prev = NULL;

    buckets[bucket_idx].head = header;
}

// Keeps or frees an empty superblock
static struct sb_header* manage_empty_sb(struct sb_header *header, int bucket_idx)
{
    if (buckets[bucket_idx].num_sb > RETAIN_FREE_SUPERBLOCK_COUNT) {
        // remove superblock from list
        if (header == buckets[bucket_idx].head) {
            // list front block is empty
            buckets[bucket_idx].head = header->next;
            buckets[bucket_idx].head->prev = NULL;
        }
        else {
            // remove superblock from list
            if (header->prev != NULL)
                header->prev->next = header->next;
            if (header->next != NULL)
                header->next->prev = header->prev;
        }
        buckets[bucket_idx].num_sb--;
        buckets[bucket_idx].free_count -= header->free_count;
        // free superblock
        kvirtual_free(header, SUPERBLOCKPAGES * PAGE_SIZE);

        header = NULL;
    }
    return header;
}

static size_t next_pow_2(size_t nextPow2)
{
    nextPow2--;
    nextPow2 |= nextPow2 >> 1;
    for (unsigned i = 2; i <= sizeof(size_t); i++) {
        nextPow2 |= nextPow2 >> i;
    }
    nextPow2++;

    return nextPow2;
}
