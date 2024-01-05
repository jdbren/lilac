#include <kernel/pgframe.h>
#include <kernel/panic.h>

#define PAGE_SIZE 0x1000
#define MEMORY_SPACE 0x100000000 // 4GB

#define CHECK_BIT(var,pos) ((var) & (1<<(pos)))
#define BITMAP_SIZE (MEMORY_SPACE / PAGE_SIZE / 8 / 4)

extern const uint32_t _kernel_end;
static const uint32_t FIRST_PAGE = (uint32_t)&_kernel_end - 0xC0000000;

static uint32_t pg_frame_bitmap[BITMAP_SIZE];

static void *__alloc_frame();
static void __free_frame(uint32_t *frame);

// bit #i in byte #n define the status of page #n*8+i
// 0 = free, 1 = used

void* alloc_frame(int num_pages) {
    printf("FIRST_PAGE: %x\n", FIRST_PAGE);
    if (num_pages == 0) {
        return 0;
    }

    if (num_pages == 1) {
        return __alloc_frame();
    }

    /*
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

                        return FIRST_PAGE + start;
                    }
                } else {
                    count = 0;
                }
            }
        } else {
            count = 0;
        }
        
    }
    */

    kerror("Out of memory");
    return 0;
}

void free_frame(void *frame, int num_pages) {
    if (num_pages == 0) {
        return;
    }

    if (num_pages == 1) {
        __free_frame(frame);
        return;
    }

    /*
    int start = ((uint32_t)frame - FIRST_PAGE) / 32;
    int end = start + num_pages;

    for (int i = start; i < end; i++) {
        int index = i / 32;
        int offset = i % 32;

        pg_frame_bitmap[index] &= ~(1 << offset);
    }
    */
}

static void* __alloc_frame() {
    static int last = 0;

    for (int i = last; i < BITMAP_SIZE; i++) {
        if (pg_frame_bitmap[i] != ~0) {
            for (int j = 0; j < 32; j++) {
                if (!CHECK_BIT(pg_frame_bitmap[i], j)) {
                    pg_frame_bitmap[i] |= (1 << j);
                    last = i;
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

static void __free_frame(uint32_t *frame) {
    int index = ((uint32_t)frame - FIRST_PAGE) / 32;
    int offset = ((uint32_t)frame - FIRST_PAGE) % 32;

    pg_frame_bitmap[index] &= ~(1 << offset);
}
