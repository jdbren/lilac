// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)

#include <lilac/config.h>
#include <lilac/types.h>
#include <lilac/libc.h>
#include <lilac/panic.h>
#include <lilac/kmalloc.h>
#include <lilac/kmm.h>
#include <lilac/pmem.h>

#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"
#pragma GCC diagnostic ignored "-Wanalyzer-use-after-free"

#define MIN_ALLOC sizeof(alloc_t)
#define MIN_ALLOC_POWER (MIN_ALLOC == 4 ? 2 : \
                        MIN_ALLOC == 8 ? 3 : -1)
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

struct __align(32) sb_header {
    struct sb_header *next;
    struct sb_header *prev;
    alloc_t *free;
    u16 canary;
    union {
        u16 alloc_size;
        u16 num_pages;
    };
    u16 free_count;
    bool is_large;
};

#ifdef DEBUG
#define DEBUG_CHECK_KMALLOC
#endif

#define verify_canary(header) \
    do { \
        if (header->canary != 0xDEAD) { \
            klog(LOG_ERROR, "kmalloc: Superblock canary mismatch at %p\n", header); \
            kerror("kmalloc: Superblock canary mismatch"); \
        } \
    } while (0)

static struct sb_list buckets[BUCKETS];

static void *malloc_small(size_t);
static void *malloc_large(size_t);
static void init_super(struct sb_header*, size_t, int);
static struct sb_header* manage_empty_sb(struct sb_header*, int);
static void sb_mtf(struct sb_header*, int);
static void sb_atf(struct sb_header*, int);
static size_t next_pow_2(size_t);

#ifdef DEBUG_CHECK_KMALLOC
static void verify_bucket_counts(void) {
    for (int i = 0; i < BUCKETS; i++) {
        u32 expected = 0;
        struct sb_header *header = buckets[i].head;

        while (header != NULL) {
            verify_canary(header);
            expected += header->free_count;
            header = header->next;
        }

        if (expected != buckets[i].free_count) {
            klog(LOG_DEBUG, "Bucket %d: free_count mismatch! expected %u, actual %u\n",
                   i, expected, buckets[i].free_count);
            kerror("kmalloc: Bucket free count mismatch");
        }
    }
}
#endif

void *kmalloc(size_t size)
{
    if (size <= 0) return NULL;
    assert(BUCKETS >= log2(SUPERBLOCKSIZE)-MIN_ALLOC_POWER);

    void *alloc = NULL;

#ifdef DEBUG_KMALLOC
    printf("kmalloc: Allocating %u bytes\n", size);
#endif
#ifdef DEBUG_CHECK_KMALLOC
    verify_bucket_counts();
#endif

    if (size > SUPERBLOCKSIZE/2)
        alloc = malloc_large(size);
    else
        alloc = malloc_small(size);

    assert(is_aligned(alloc, MIN_ALLOC));
#ifdef DEBUG_KMALLOC
    printf("kmalloc: Allocated %u bytes at %p\n", size, alloc);
#endif
#ifdef DEBUG_CHECK_KMALLOC
    verify_bucket_counts();
#endif
    if (unlikely(alloc == NULL))
        kerror("kmalloc failed\n");

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
    uintptr_t base = (uintptr_t)addr & PAGE_MASK;
    struct sb_header *header = (struct sb_header*)base;
    assert(is_aligned(header, PAGE_SIZE));

    // check if large allocation
    if (header->is_large) {
        if (size <= header->num_pages * PAGE_SIZE - sizeof(struct sb_header))
            return addr;
        void *new = kmalloc(size);
        if (new == NULL) return NULL;
        memcpy(new, addr, header->num_pages * PAGE_SIZE - sizeof(struct sb_header));
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
    if (new == NULL) return NULL;
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

#ifdef __x86_64__
    assert(is_canonical((uintptr_t)ptr));
#endif
#ifdef DEBUG_KMALLOC
    printf("kfree: Freeing memory at %p\n", ptr);
#endif
#ifdef DEBUG_CHECK_KMALLOC
    verify_bucket_counts();
#endif
    // get superblock header using bitmask
    uintptr_t base = (uintptr_t)ptr & PAGE_MASK;
    struct sb_header *header = (struct sb_header*)base;
    assert(is_aligned(header, PAGE_SIZE));

    // check if large allocation
    if (header->is_large) {
        free_pages(header, header->num_pages);
        return;
    }

    // get bucket index and step size
    int step = header->alloc_size;
    int bucket_idx = log2(step) - MIN_ALLOC_POWER;

    assert(is_aligned(ptr, header->alloc_size));

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
#ifdef DEBUG_KMALLOC
    klog(LOG_DEBUG, "kfree: Freed memory at %p, bucket %d, free_count %d\n",
           ptr, bucket_idx, buckets[bucket_idx].free_count);
#endif
#ifdef DEBUG_CHECK_KMALLOC
    verify_bucket_counts();
#endif
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
        void *block = get_free_pages(SUPERBLOCKSIZE / PAGE_SIZE, 0);
        assert(block != NULL);

        header = (struct sb_header*)block;
        // initialize superblock stats and free list
        init_super(header, size, bucket_idx);

        // add superblock to front of list
        sb_atf(header, bucket_idx);
    } else {
        // use existing superblock
        header = (struct sb_header*)buckets[bucket_idx].head;
        while (header->free_count == 0) {
            if (header->next == NULL) {
                klog(LOG_ERROR , "kmalloc: No free memory in bucket %d\n", bucket_idx);
                kerror("");
            } else {
                header = header->next;
            }
        }
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

    void *block = get_free_pages(pages, 0);
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
    header->next = NULL;
    header->prev = NULL;
    header->is_large = false;
    header->canary = 0xDEAD;
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
    alloc_t *current = header->free, *next = NULL;
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
            assert(header->next != NULL);
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
        assert(header->free_count == (SUPERBLOCKSIZE - sizeof(struct sb_header)) / header->alloc_size);
        buckets[bucket_idx].free_count -= header->free_count;
        // free superblock
        free_pages(header, SUPERBLOCKPAGES);

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
