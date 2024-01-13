#include <kernel/mm.h>
#include <kernel/pgframe.h>
#include <kernel/paging.h>
#include <kernel/panic.h>

typedef struct memory_desc {
    uint32_t *next, *prev;
    uint32_t size;
    uint32_t *start;
} memory_desc;

memory_desc *kernel_avail_head;

void mm_init() {
    kernel_avail_head = 

}

void* virtual_alloc(int num_pages) {
    
}

void virtual_free(void* addr, int num_pages) {

}
