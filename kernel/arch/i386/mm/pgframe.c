#include <mm/pgframe.h>
#include <kernel/panic.h>

#define PAGE_SIZE 0x1000
#define MEMORY_SPACE 0x100000000 // 4GB

#define CHECK_BIT(var,pos) ((var) & (1<<(pos)))
#define BITMAP_SIZE (MEMORY_SPACE / PAGE_SIZE / 8 / 4)



typedef struct {
    uint8_t pg[PAGE_SIZE];
} __attribute__((packed)) page_t;

extern const uint32_t _kernel_end;
static uint32_t FIRST_PAGE = (uint32_t)&_kernel_end - (uint32_t)0xC0000000;

static uint32_t pg_frame_bitmap[BITMAP_SIZE];

static void *__alloc_frame();
static void __free_frame(page_t *frame);

// bit #i in byte #n define the status of page #n*8+i
// 0 = free, 1 = used

void* alloc_frames(uint32_t num_pages) {
    if (num_pages == 0) {
        return 0;
    }

    if (num_pages == 1) {
        return __alloc_frame();
    }

    int count = 0;
    int start = 0;

    for (int i = 0; i < BITMAP_SIZE; i++) {
        if (pg_frame_bitmap[i] != ~0) {
            for (int j = 0; j < 32; j++) {
                if (!CHECK_BIT(pg_frame_bitmap[i], j)) {
                    if (count == 0) {
                        start = i * 32 + j;
                    }

                    count++;

                    if (count == num_pages) {
                        for (int k = start; k < start + num_pages; k++) {
                            int index = k / 32;
                            int offset = k % 32;

                            pg_frame_bitmap[index] |= (1 << offset);
                        }
                        printf("Allocated phys pages %x to %x\n", 
                            (void*)(FIRST_PAGE + start * PAGE_SIZE), 
                            (void*)(FIRST_PAGE + (start + num_pages) * PAGE_SIZE));
                        return (void*)(FIRST_PAGE + start * PAGE_SIZE);
                    }
                } else {
                    count = 0;
                }
            }
        } else {
            count = 0;
        }
        
    }

    kerror("Out of memory");
    return 0;
}

void free_frames(void *frame, uint32_t num_pages) {
    if (num_pages == 0) {
        return;
    }

    if (num_pages == 1) {
        __free_frame(frame);
        return;
    }

    page_t *end = (page_t*)frame + num_pages;

    for (page_t *pg = (page_t*)frame; pg < end; pg++) {
        __free_frame(pg);
    }
}

static void* __alloc_frame() {
    static int last = 0;

    for (int i = last; i < BITMAP_SIZE; i++) {
        if (pg_frame_bitmap[i] != ~0) {
            for (int j = 0; j < 32; j++) {
                if (!CHECK_BIT(pg_frame_bitmap[i], j)) {
                    pg_frame_bitmap[i] |= (1 << j);
                    last = i;
                    printf("Allocated phys page %x\n", (void*)(FIRST_PAGE + (i * 32 + j) * PAGE_SIZE));
                    return (void*)(FIRST_PAGE + (i * 32 + j) * PAGE_SIZE);
                }
            }
        }
    }

    for (int i = 0; i < last; i++) {
        if (pg_frame_bitmap[i] != 0) {
            for (int j = 0; j < 32; j++) {
                if (!CHECK_BIT(pg_frame_bitmap[i], j)) {
                    pg_frame_bitmap[i] |= (1 << j);
                    last = i;
                    return (void*)(FIRST_PAGE + (i * 32 + j) * PAGE_SIZE);
                }
            }
        }
    }

    kerror("Out of memory");
    return 0;
}

static void __free_frame(page_t *frame) {
    printf("Freeing phys page %x\n", frame);

    uint32_t index = ((uint32_t)frame - FIRST_PAGE) / (32 * PAGE_SIZE);
    uint32_t offset = (((uint32_t)frame - FIRST_PAGE) / PAGE_SIZE) % 32;

    pg_frame_bitmap[index] &= ~(1 << offset);
}
