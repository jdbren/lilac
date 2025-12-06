#include <mm/page.h>
#include <mm/kmm.h>
#include <utility/efi.h>
#include <lilac/boot.h>
#include <lilac/sync.h>

#define check_bit(var,pos) ((var) & (1ul<<(pos)))
#define get_index(frame) ((uintptr_t)frame / (BITS_PER_LONG * PAGE_SIZE))
#define get_offset(frame) (((uintptr_t)frame / PAGE_SIZE) % BITS_PER_LONG)

typedef u8 page_t[PAGE_SIZE];

static uintptr_t FIRST_PAGE = 0x0;
static u32 BITMAP_SIZE;
static volatile size_t *pg_frame_bitmap;
static volatile spinlock_t pg_frame_bm_lock = SPINLOCK_INIT;

static void *__check_bitmap(int i, int num_pages, int *count, int *start);
static void *__do_frame_alloc(int, int);
static void __free_frame(page_t *frame);
static void __mark_frames(size_t index, size_t offset, size_t pg_cnt);

struct page *phys_frames;

static void efi_init_bitmap(void)
{
    struct multiboot_tag_efi_mmap *mmap = boot_info.mbd.efi_mmap;

    // Mark some reserved frames
    __mark_frames(get_index(0x0), get_offset(0x0), 0x100000 / PAGE_SIZE);
    __mark_frames(get_index(__phys_addr(&_kernel_start)),
        get_offset(__phys_addr(&_kernel_start)),
        (__phys_addr(&_kernel_end) - __phys_addr(&_kernel_start)) / PAGE_SIZE + 1);
    __mark_frames(get_index(__phys_addr(pg_frame_bitmap)),
        get_offset(__phys_addr(pg_frame_bitmap)),
        BITMAP_SIZE / PAGE_SIZE + 1);

    efi_memory_desc_t *entry = (efi_memory_desc_t*)mmap->efi_mmap;
    for (u32 i = 0; i < mmap->size;
        i += mmap->descr_size,
        entry = (efi_memory_desc_t*)((uintptr_t)entry + mmap->descr_size)) {

        switch (entry->type) {
            case EFI_CONVENTIONAL_MEMORY:
            case EFI_BOOT_SERVICES_CODE:
            case EFI_BOOT_SERVICES_DATA:
            case EFI_LOADER_CODE:
            case EFI_LOADER_DATA:
                continue;
            default:
                if (entry->phys_addr >= boot_info.total_mem_kb * 1024)
                    continue;
                __mark_frames(get_index(entry->phys_addr),
                            get_offset(entry->phys_addr),
                            entry->num_pages);
                break;
        }
    }
}


void pmem_init(void)
{
    u32 total_pages = boot_info.total_mem_kb / (PAGE_SIZE / 1024);
    BITMAP_SIZE = total_pages / 8 + 8; // in bytes
    pg_frame_bitmap = arch_map_frame_bitmap(BITMAP_SIZE);
    memset((void*)pg_frame_bitmap, 0, BITMAP_SIZE);
    efi_init_bitmap();

    void *phys = alloc_frames(PAGE_UP_COUNT(total_pages * sizeof(struct page)));
    phys_frames = (struct page *)(phys_mem_mapping + (uintptr_t)phys);
}


struct page * alloc_pages(u32 pgcnt, u32 flags)
{
    uintptr_t phys = (uintptr_t)alloc_frames(pgcnt);
    if (!phys)
        return NULL;

    struct page *pg = phys_to_page(phys);
    pg->refcount = 1;
    return pg;
}

void __free_pages(struct page *frame, u32 pgcnt)
{
    uintptr_t addr = page_to_phys(frame);
    free_frames((void*)addr, pgcnt);
}

void free_pages(void *addr, u32 pgcnt)
{
    struct page *pg = virt_to_page(addr);
    __free_pages(pg, pgcnt);
}

void * get_free_pages(u32 pgcnt, u32 flags)
{
    struct page *pg = alloc_pages(pgcnt, flags);
    return pg ? get_page_addr(pg) : NULL;
}

void * get_zeroed_pages(u32 pgcnt, u32 flags)
{
    void *addr = get_free_pages(pgcnt, flags);
    if (addr)
        memset(addr, 0, pgcnt * PAGE_SIZE);
    return addr;
}

// --------

void* alloc_frames(u32 num_pages)
{
    if (num_pages == 0)
        return 0;

    acquire_lock(&pg_frame_bm_lock);

    void *ptr = NULL;
    int start = 0;
    int count = 0;
    for (size_t i = 0; i < BITMAP_SIZE / sizeof(size_t); i++) {
        if (pg_frame_bitmap[i] != ~0UL) {
            ptr = __check_bitmap(i, num_pages, &count, &start);
            if (ptr) {
#ifdef DEBUG_PAGING
                klog(LOG_DEBUG, "Allocated %d physical frames at %p\n", num_pages, ptr);
#endif
                break;
            }
        }
        else
            count = 0;
    }

    if (ptr == NULL)
        panic("Out of memory");

    release_lock(&pg_frame_bm_lock);
    return ptr;
}

void free_frames(void *frame, u32 num_pages)
{
    if (num_pages == 0)
        return;

    acquire_lock(&pg_frame_bm_lock);

    for (page_t *pg = (page_t*)frame, *end = (page_t*)frame + num_pages;
    pg < end; pg++) {
        __free_frame(pg);
    }
#ifdef DEBUG_PAGING
    klog(LOG_DEBUG, "Freed %d physical frames at %p\n", num_pages, frame);
#endif
    release_lock(&pg_frame_bm_lock);
}

static void __mark_frames(size_t index, size_t offset, size_t pg_cnt)
{
    while (offset < BITS_PER_LONG && pg_cnt > 0) {
        pg_frame_bitmap[index] |= (1UL << offset);
        offset++;
        pg_cnt--;
    }
    index++;
    while (pg_cnt >= BITS_PER_LONG) {
        pg_frame_bitmap[index++] = ~0UL;
        pg_cnt -= BITS_PER_LONG;
    }
    offset = 0;
    while (pg_cnt > 0) {
        pg_frame_bitmap[index] |= (1UL << offset++);
        pg_cnt--;
    }
}


static void* __check_bitmap(int i, int num_pages, int *count, int *start)
{
    for (int j = 0; j < BITS_PER_LONG; j++) {
        if (!check_bit(pg_frame_bitmap[i], j)) {
            if (*count == 0)
                *start = i * BITS_PER_LONG + j;

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
        int index = k / BITS_PER_LONG;
        int offset = k % BITS_PER_LONG;

        pg_frame_bitmap[index] |= (1UL << offset);
    }

    return (void*)(FIRST_PAGE + start * PAGE_SIZE);
}

static void __free_frame(page_t *frame)
{
    u32 index = get_index(frame);
    u32 offset = get_offset(frame);

    pg_frame_bitmap[index] &= ~(1UL << offset);
}

//
// Debugging functions
//

void print_bitmap(void)
{
    for (size_t i = 0; i < BITMAP_SIZE / sizeof(u32); i++) {
        for (int j = 0; j < BITS_PER_LONG; j++) {
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
    for (size_t i = 0; i < BITMAP_SIZE / sizeof(size_t); i++) {
        for (int j = 0; j < BITS_PER_LONG; j++) {
            if (!check_bit(pg_frame_bitmap[i], j))
                count++;
        }
    }
    return count;
}

int num_used_frames(void)
{
    int count = 0;
    for (size_t i = 0; i < BITMAP_SIZE / sizeof(size_t); i++) {
        for (int j = 0; j < BITS_PER_LONG; j++) {
            if (check_bit(pg_frame_bitmap[i], j))
                count++;
        }
    }
    return count;
}
