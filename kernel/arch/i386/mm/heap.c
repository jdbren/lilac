#include <stdint.h>
#include <stdbool.h>
#include <kernel/heap.h>
#include <kernel/paging.h>
#include <kernel/pgframe.h>

#define PAGE_SIZE PAGE_BYTES
#define KHEAP_MAX_ADDR 0xFFFEF000
#define KHEAP_PAGES KHEAP_SIZE / PAGE_SIZE

extern uint32_t _kernel_end;
const uint32_t KHEAP_START_ADDR = (uint32_t)(&_kernel_end);

#define MIN_ALLOC 32
#define MIN_ALLOC_POWER 5
#define SUPERBLOCKSIZE PAGE_SIZE
#define BUCKETS 7
#define RETAIN_FREE_SUPERBLOCK_COUNT 10
#define SUPERBLOCKMASK (~(SUPERBLOCKSIZE - 1))
#define is_aligned(POINTER, BYTE_COUNT) \
    (((uintptr_t)(const void *)(POINTER)) % (BYTE_COUNT) == 0)

typedef struct SBList SBList;
typedef struct SBHeader SBHeader;
typedef union Alloc Alloc;
typedef unsigned char byte;

struct SBList {
    SBHeader *list;
    unsigned int allocSize;
    unsigned int freeCount;
    unsigned int numSB;
    unsigned int numFreeSB;
};

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
    char padding[2];
}; // 32 bytes

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

void heap_initialize(void) {
    void *addr_storage = alloc_frame(0x400);
    map_page(addr_storage, KHEAP_START_ADDR, 0x3);
    page_list->next = 0;
    page_list->prev = 0;
    page_list->space = PAGE_SIZE - sizeof(SmallHeader);
    page_list->largest_gap = page_list->space;
    page_list->free_list = KHEAP_START_ADDR;
    
}

void* kmalloc(int size) {
    
}

void kfree(void *addr) {

}

void* malloc_small(size_t size) {
    totalSmallRequested += size;

    // round up to next power of 2
    size = size < MIN_ALLOC ? MIN_ALLOC : nextPowerOfTwo(size);
    assert(size <= SUPERBLOCKSIZE/2);

    int bucketIndex = (int)log2(size) - MIN_ALLOC_POWER;
    Alloc *alloc = NULL;
    SBHeader *header = NULL;

    // check if there is any memory available
    if (allLists[bucketIndex].freeCount == 0) {
        // allocate new superblock
        void *block = mmap(NULL, SUPERBLOCKSIZE, PROT_READ|PROT_WRITE,
            MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (block == MAP_FAILED) {
            perror("mmap failed");
            return NULL;
        }
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
        while (header->freeCount == 0) {
            header = header->nextSB;
        }
        // move superblock to front of list if it has room
        // trying to improve locality, I think Hoard does something similar
        if (header->freeCount > 1 && header != allLists[bucketIndex].list) {
            moveToFront(header, bucketIndex);
        }
    }

    if (header->isFree) {
        allLists[bucketIndex].numFreeSB--;
        header->isFree = false;
    }
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

// Initialize a superblock and its free list
void initSuper(SBHeader *header, size_t size, int bucketIndex) {
    // make superblock header
    header->free = (Alloc*)((byte*)header + size);
    header->allocSize = size;
    header->freeCount = (SUPERBLOCKSIZE - sizeof(SBHeader)) / size;
    header->isFree = true;
    // update bucket info
    allLists[bucketIndex].freeCount += header->freeCount;
    allLists[bucketIndex].numSB++;
    allLists[bucketIndex].numFreeSB++;
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
void moveToFront(SBHeader *header, int bucketIndex) {
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
void addToFront(SBHeader *header, int bucketIndex) {
    header->nextSB = allLists[bucketIndex].list;
    if (header->nextSB != NULL)
        header->nextSB->prevSB = header;
    header->prevSB = NULL;

    allLists[bucketIndex].list = header;
}

// Keeps or frees an empty superblock
SBHeader* manageEmptySuperblock(SBHeader *header, int bucketIndex) {
    if (allLists[bucketIndex].numSB <= RETAIN_FREE_SUPERBLOCK_COUNT) {
        // keep superblock in list
        allLists[bucketIndex].numFreeSB++;
        header->isFree = true;
        assert(allLists[bucketIndex].numFreeSB <= allLists[bucketIndex].numSB);
        assert(allLists[bucketIndex].numFreeSB <= RETAIN_FREE_SUPERBLOCK_COUNT);
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
        munmap(header, SUPERBLOCKSIZE);
        if (errno != 0) {
            perror("munmap failed");
            return NULL;
        }
        header = NULL;
    }
    return header;
}

// Get the next power of 2
// https://stackoverflow.com/questions/466204/rounding-up-to-next-power-of-2
size_t nextPowerOfTwo(size_t nextPow2){
    nextPow2--;
    nextPow2 |= nextPow2 >> 1;
    for (int i = 2; i < sizeof(size_t); i++) {
        nextPow2 |= nextPow2 >> i;
    }
    nextPow2++;

    return nextPow2;
}
