#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include <kernel/panic.h>
#include <mm/kheap.h>
#include <mm/kmm.h>
#include <mm/paging.h>
#include <mm/pgframe.h>

#define PAGE_SIZE PAGE_BYTES
#define MIN_ALLOC 4
#define MIN_ALLOC_POWER 2
#define RETAIN_FREE_SUPERBLOCK_COUNT 4
#define PAGE_MASK (~(PAGE_SIZE - 1))
#define is_aligned(POINTER, BYTE_COUNT) \
    (((uintptr_t)(POINTER)) % (BYTE_COUNT) == 0)

static const int SUPERBLOCKSIZE = PAGE_SIZE * 8;
static const int SUPERBLOCKPAGES = SUPERBLOCKSIZE / PAGE_SIZE;
#define BUCKETS 13

typedef struct SBList SBList;
typedef struct SBHeader SBHeader;
typedef union Alloc Alloc;
typedef unsigned char byte;

struct SBList {
    SBHeader *list;
    uint32_t allocSize;
    uint32_t freeCount;
    uint32_t numSB;
}; // 16 bytes

struct SBHeader {
    SBHeader *nextSB;
    SBHeader *prevSB;
    Alloc *free;
    union {
        unsigned short allocSize;
        unsigned short numPages;
    };
    unsigned short freeCount;
    bool isFree;
    bool isLarge;
} __attribute__((aligned(32))); 

union Alloc {
    Alloc *free;
    Alloc *data;
};

static SBList allLists[BUCKETS];

static unsigned long totalSmallRequested;
static unsigned long totalSmallAllocated;

static unsigned long totalLargeRequested;
static unsigned totalPages;
static unsigned currentPages;

void* malloc_small(size_t);
void* malloc_large(size_t);
void initSuper(SBHeader*, size_t, int);
SBHeader* manageEmptySuperblock(SBHeader*, int);
void moveToFront(SBHeader*, int);
void addToFront(SBHeader*, int);
size_t nextPowerOfTwo(size_t);


void* kmalloc(int size)
{
    if(size <= 0) return NULL;
    assert(BUCKETS >= log2(SUPERBLOCKSIZE)-MIN_ALLOC_POWER);
    
    void *alloc = NULL;

    if(size > SUPERBLOCKSIZE/2)
        alloc = malloc_large(size);
    else
        alloc = malloc_small(size);

    assert(is_aligned(alloc, MIN_ALLOC));

    return alloc;
}

void kfree(void *ptr)
{
    if(ptr == NULL) return;

    // get superblock header using bitmask
    uint32_t base = (uint32_t)ptr & PAGE_MASK;
    SBHeader *header = (SBHeader*)base;
    assert(is_aligned(header, PAGE_SIZE));

    // check if large allocation
    if(header->isLarge) {
        currentPages -= header->numPages;
        kvirtual_free(header, header->numPages);
        return;
    }

    // get bucket index and step size
    int step = header->allocSize;
    int bucketIndex = log2(step) - MIN_ALLOC_POWER;

    // return alloc to free list
    Alloc *alloc = (Alloc*)ptr;
    alloc->data = header->free;
    header->free = alloc;
    header->freeCount++;
    allLists[bucketIndex].freeCount++;

    assert(header->freeCount <= (SUPERBLOCKSIZE - sizeof(SBHeader)) / step);
        
    // check if superblock is empty
    if(header->freeCount == (SUPERBLOCKSIZE - sizeof(SBHeader)) / step)
        header = manageEmptySuperblock(header, bucketIndex);
    // move superblock to front of list
    if(header != NULL && !(header == allLists[bucketIndex].list))
        moveToFront(header, bucketIndex);
}

void* malloc_small(size_t size)
{
    totalSmallRequested += size;

    // round up to next power of 2
    size = size < MIN_ALLOC ? MIN_ALLOC : nextPowerOfTwo(size);
    assert(size <= SUPERBLOCKSIZE/2);

    int bucketIndex = log2(size) - MIN_ALLOC_POWER;
    Alloc *alloc = NULL;
    SBHeader *header = NULL;

    // check if there is any memory available
    if (allLists[bucketIndex].freeCount == 0) {
        // allocate new superblock
        void *block = kvirtual_alloc(SUPERBLOCKPAGES);
        assert(block != NULL);

        header = (SBHeader*)block;
        // initialize superblock stats and free list
        initSuper(header, size, bucketIndex);

        // add superblock to front of list
        addToFront(header, bucketIndex);
    }
    else {
        // use existing superblock
        header = (SBHeader*)allLists[bucketIndex].list;
        while (header->freeCount == 0)
            header = header->nextSB;
        // move superblock to front of list if it has room
        // trying to improve locality, I think Hoard does something similar
        if (header->freeCount > 1 && header != allLists[bucketIndex].list)
            moveToFront(header, bucketIndex);
    }

    if (header->isFree)
        header->isFree = false;
    assert(header->freeCount > 0);
    assert(allLists[bucketIndex].allocSize == size);

    // get alloc from free list
    alloc = header->free;

    // update free list
    header->free = alloc->free;
    header->freeCount--;
    allLists[bucketIndex].freeCount--;
    
    totalSmallAllocated += size;
    assert(is_aligned(alloc, size));
    return (void*)alloc;
}

// allocate large memory using mmap
void* malloc_large(size_t size)
{
    Alloc *alloc = NULL;
    totalLargeRequested += size;
    int pages = (size + sizeof(SBHeader))/PAGE_SIZE + 1;
    size = pages * PAGE_SIZE;

    void *block = kvirtual_alloc(pages);
    assert(block != NULL);

    // set up superblock header
    SBHeader *header = (SBHeader*)block;
    header->isLarge = true;
    header->numPages = pages;
    header->free = (Alloc*)((byte*)header + sizeof(SBHeader));
    alloc = header->free;

    // update stats
    totalPages += pages;
    currentPages += pages;

    return (void*)alloc;
}

// Initialize a superblock and its free list
void initSuper(SBHeader *header, size_t size, int bucketIndex)
{
    // make superblock header
    if (size > sizeof(SBHeader))
        header->free = (Alloc*)((byte*)header + size);
    else
        header->free = (Alloc*)(header + 1);
    header->allocSize = size;
    header->freeCount = (SUPERBLOCKSIZE - sizeof(SBHeader)) / size;
    header->isFree = true;
    // update bucket info
    allLists[bucketIndex].freeCount += header->freeCount;
    allLists[bucketIndex].numSB++;
    allLists[bucketIndex].allocSize = size;

    // make free list
    Alloc *current = header->free, *next = 0;
    for (int i = 1; i < header->freeCount; i++) {
        next = (Alloc*)((byte*)(current) + size);
        current->free = next;
        current = next;
    }
}

// Move superblock to front of list
void moveToFront(SBHeader *header, int bucketIndex)
{
    if (header->prevSB != NULL)
        header->prevSB->nextSB = header->nextSB;
    if (header->nextSB != NULL)
        header->nextSB->prevSB = header->prevSB;
    header->nextSB = allLists[bucketIndex].list;
    if (header->nextSB != NULL)
        header->nextSB->prevSB = header;
    header->prevSB = NULL;
    allLists[bucketIndex].list = header;
}

// Add superblock to front of list
void addToFront(SBHeader *header, int bucketIndex)
{
    header->nextSB = allLists[bucketIndex].list;
    if (header->nextSB != NULL)
        header->nextSB->prevSB = header;
    header->prevSB = NULL;

    allLists[bucketIndex].list = header;
}

// Keeps or frees an empty superblock
SBHeader* manageEmptySuperblock(SBHeader *header, int bucketIndex)
{
    if (allLists[bucketIndex].numSB <= RETAIN_FREE_SUPERBLOCK_COUNT) {
        // keep superblock in list
        header->isFree = true;
    }
    else {
        // remove superblock from list
        if (header == allLists[bucketIndex].list) {
            // list front block is empty
            allLists[bucketIndex].list = header->nextSB;
            allLists[bucketIndex].list->prevSB = NULL;
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
        kvirtual_free(header, SUPERBLOCKPAGES);

        header = NULL;
    }
    return header;
}

// Get the next power of 2
// https://stackoverflow.com/questions/466204/rounding-up-to-next-power-of-2
size_t nextPowerOfTwo(size_t nextPow2)
{
    nextPow2--;
    nextPow2 |= nextPow2 >> 1;
    for (unsigned i = 2; i < sizeof(size_t); i++) {
        nextPow2 |= nextPow2 >> i;
    }
    nextPow2++;

    return nextPow2;
}
