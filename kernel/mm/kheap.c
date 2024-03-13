// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#include <kernel/config.h>
#include <kernel/types.h>
#include <kernel/panic.h>
#include <mm/kheap.h>
#include <mm/kmm.h>

#define MIN_ALLOC 4
#define MIN_ALLOC_POWER 2
#define RETAIN_FREE_SUPERBLOCK_COUNT 4
#define PAGE_MASK (~(PAGE_SIZE - 1))
#define is_aligned(POINTER, BYTE_COUNT) \
    (((uintptr_t)(POINTER)) % (BYTE_COUNT) == 0)

#define SUPERBLOCKSIZE PAGE_SIZE
#define SUPERBLOCKPAGES (SUPERBLOCKSIZE / PAGE_SIZE)
#define BUCKETS 13

typedef union Alloc {
    void *next;
    void *data;
} alloc_t;

struct SBList {
    struct SBHeader *head;
    u32 allocSize;
    u32 freeCount;
    u32 numSB;
};

struct SBHeader {
    struct SBHeader *nextSB;
    struct SBHeader *prevSB;
    alloc_t *free;
    union {
        u16 allocSize;
        u16 numPages;
    };
    u16 freeCount;
    bool isLarge;
} __attribute__((aligned(32)));


static struct SBList allLists[BUCKETS];

static void *malloc_small(size_t);
static void *malloc_large(size_t);
static void initSuper(struct SBHeader*, size_t, int);
static struct SBHeader* manageEmptySuperblock(struct SBHeader*, int);
static void moveToFront(struct SBHeader*, int);
static void addToFront(struct SBHeader*, int);
static size_t nextPowerOfTwo(size_t);


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
    struct SBHeader *header = (struct SBHeader*)base;
    assert(is_aligned(header, PAGE_SIZE));

    // check if large allocation
    if (header->isLarge) {
        void *newAddr = kmalloc(size);
        memcpy(newAddr, addr, size);
        kfree(addr);
        return newAddr;
    }

    // get bucket index and step size
    u32 step = header->allocSize;

    // check if new size is in same bucket
    if (size <= header->allocSize)
        return addr;

    // allocate new memory
    void *newAddr = kmalloc(size);
    memcpy(newAddr, addr, step);
    kfree(addr);
    return newAddr;
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
    struct SBHeader *header = (struct SBHeader*)base;
    assert(is_aligned(header, PAGE_SIZE));

    // check if large allocation
    if (header->isLarge) {
        kvirtual_free(header, header->numPages * PAGE_SIZE);
        return;
    }

    // get bucket index and step size
    int step = header->allocSize;
    int bucketIndex = log2(step) - MIN_ALLOC_POWER;

    // return alloc to free list
    alloc_t *alloc = (alloc_t*)ptr;
    alloc->data = header->free;
    header->free = alloc;
    header->freeCount++;
    allLists[bucketIndex].freeCount++;

    assert(header->freeCount <= (SUPERBLOCKSIZE - sizeof(struct SBHeader)) / step);

    // check if superblock is empty
    if (header->freeCount == (SUPERBLOCKSIZE - sizeof(struct SBHeader)) / step)
        header = manageEmptySuperblock(header, bucketIndex);
    // move superblock to front of list
    if (header != NULL && !(header == allLists[bucketIndex].head))
        moveToFront(header, bucketIndex);
}

static void* malloc_small(size_t size)
{
    // round up to next power of 2
    size = size < MIN_ALLOC ? MIN_ALLOC : nextPowerOfTwo(size);
    assert(size <= SUPERBLOCKSIZE/2);

    int bucketIndex = log2(size) - MIN_ALLOC_POWER;
    alloc_t *alloc = NULL;
    struct SBHeader *header = NULL;

    // check if there is any memory available
    if (allLists[bucketIndex].freeCount == 0) {
        // allocate new superblock
        void *block = kvirtual_alloc(SUPERBLOCKPAGES * PAGE_SIZE, PG_WRITE);
        assert(block != NULL);

        header = (struct SBHeader*)block;
        // initialize superblock stats and free list
        initSuper(header, size, bucketIndex);

        // add superblock to front of list
        addToFront(header, bucketIndex);
    }
    else {
        // use existing superblock
        header = (struct SBHeader*)allLists[bucketIndex].head;
        while (header->freeCount == 0)
            header = header->nextSB;
        // move superblock to front of list if it has room
        // trying to improve locality, I think Hoard does something similar
        if (header->freeCount > 1 && header != allLists[bucketIndex].head)
            moveToFront(header, bucketIndex);
    }

    assert(header->freeCount > 0);
    assert(allLists[bucketIndex].allocSize == size);

    // get alloc from free list
    alloc = header->free;

    // update free list
    header->free = alloc->next;
    header->freeCount--;
    allLists[bucketIndex].freeCount--;

    assert(is_aligned(alloc, size));
    return (void*)alloc;
}

// allocate large memory
static void* malloc_large(size_t size)
{
    alloc_t *alloc = NULL;
    int pages = (size + sizeof(struct SBHeader))/PAGE_SIZE + 1;
    size = pages * PAGE_SIZE;

    void *block = kvirtual_alloc(size, PG_WRITE);
    assert(block != NULL);

    // set up superblock header
    struct SBHeader *header = (struct SBHeader*)block;
    header->isLarge = true;
    header->numPages = pages;
    header->free = (alloc_t*)((u8*)header + sizeof(struct SBHeader));
    alloc = header->free;

    return (void*)alloc;
}

// Initialize a superblock and its free list
static void initSuper(struct SBHeader *header, size_t size, int bucketIndex)
{
    // make superblock header
    if (size > sizeof(struct SBHeader))
        header->free = (alloc_t*)((u8*)header + size);
    else
        header->free = (alloc_t*)(header + 1);
    header->allocSize = size;
    header->freeCount = (SUPERBLOCKSIZE - sizeof(struct SBHeader)) / size;
    // update bucket info
    allLists[bucketIndex].freeCount += header->freeCount;
    allLists[bucketIndex].numSB++;
    allLists[bucketIndex].allocSize = size;

    // make free list
    alloc_t *current = header->free, *next = 0;
    for (int i = 1; i < header->freeCount; i++) {
        next = (alloc_t*)((u8*)(current) + size);
        current->next = next;
        current = next;
    }
}

// Move superblock to front of list
static void moveToFront(struct SBHeader *header, int bucketIndex)
{
    if (header->prevSB != NULL)
        header->prevSB->nextSB = header->nextSB;
    if (header->nextSB != NULL)
        header->nextSB->prevSB = header->prevSB;
    header->nextSB = allLists[bucketIndex].head;
    if (header->nextSB != NULL)
        header->nextSB->prevSB = header;
    header->prevSB = NULL;
    allLists[bucketIndex].head = header;
}

// Add superblock to front of list
static void addToFront(struct SBHeader *header, int bucketIndex)
{
    header->nextSB = allLists[bucketIndex].head;
    if (header->nextSB != NULL)
        header->nextSB->prevSB = header;
    header->prevSB = NULL;

    allLists[bucketIndex].head = header;
}

// Keeps or frees an empty superblock
static struct SBHeader* manageEmptySuperblock(struct SBHeader *header, int bucketIndex)
{
    if (allLists[bucketIndex].numSB > RETAIN_FREE_SUPERBLOCK_COUNT) {
        // remove superblock from list
        if (header == allLists[bucketIndex].head) {
            // list front block is empty
            allLists[bucketIndex].head = header->nextSB;
            allLists[bucketIndex].head->prevSB = NULL;
        }
        else {
            // remove superblock from list
            if (header->prevSB != NULL)
                header->prevSB->nextSB = header->nextSB;
            if (header->nextSB != NULL)
                header->nextSB->prevSB = header->prevSB;
        }
        allLists[bucketIndex].numSB--;
        allLists[bucketIndex].freeCount -= header->freeCount;
        // free superblock
        kvirtual_free(header, SUPERBLOCKPAGES * PAGE_SIZE);

        header = NULL;
    }
    return header;
}

static size_t nextPowerOfTwo(size_t nextPow2)
{
    nextPow2--;
    nextPow2 |= nextPow2 >> 1;
    for (unsigned i = 2; i < sizeof(size_t); i++) {
        nextPow2 |= nextPow2 >> i;
    }
    nextPow2++;

    return nextPow2;
}
