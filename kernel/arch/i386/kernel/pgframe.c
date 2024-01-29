#include <pgframe.h>
#include <kernel/panic.h>

#define PAGE_SIZE 0x1000
#define MEMORY_SPACE 0x100000000 // 4GB
#define BITMAP_SIZE (MEMORY_SPACE / PAGE_SIZE / 8 / 4)

#define check_bit(var,pos) ((var) & (1<<(pos)))

typedef struct {
    u8 pg[PAGE_SIZE];
} __attribute__((packed)) page_t;

extern const u32 _kernel_end;
static const u32 FIRST_PAGE = (u32)&_kernel_end - (u32)0xC0000000;

static u32 pg_frame_bitmap[BITMAP_SIZE];

static void *__check_bitmap(int i, int num_pages, int *count, int *start);
static void *__do_frame_alloc(int, int);
static void __free_frame(page_t *frame);

// bit #i in byte #n define the status of page #n*8+i
// 0 = free, 1 = used

void* alloc_frames(u32 num_pages) 
{
    if (num_pages == 0) 
        return 0;

    int start = 0;
    int count = 0;
    for (int i = 0; i < BITMAP_SIZE; i++) {
        if (pg_frame_bitmap[i] != ~0UL) {
            void *ptr = __check_bitmap(i, num_pages, &count, &start);
            if (ptr)
                return ptr;
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

    // printf("Allocated phys pages %x to %x\n", 
    //     (void*)(FIRST_PAGE + start * PAGE_SIZE), 
    //     (void*)(FIRST_PAGE + (start + num_pages) * PAGE_SIZE));
    
    return (void*)(FIRST_PAGE + start * PAGE_SIZE);
}

static void __free_frame(page_t *frame)
{
    u32 index = ((u32)frame - FIRST_PAGE) / (32 * PAGE_SIZE);
    u32 offset = (((u32)frame - FIRST_PAGE) / PAGE_SIZE) % 32;

    pg_frame_bitmap[index] &= ~(1 << offset);
}
