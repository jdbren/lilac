#include <string.h>

#include <mm/kmm.h>
#include <mm/pgframe.h>
#include <mm/paging.h>
#include <kernel/panic.h>

#define PAGE_SIZE PAGE_BYTES
#define KHEAP_START_ADDR 0xC1000000
#define KHEAP_MAX_ADDR 0xFEFFF000
#define KHEAP_PAGES ((KHEAP_MAX_ADDR - KHEAP_START_ADDR) / PAGE_SIZE)
#define get_index(VIRT_ADDR) (KHEAP_MAX_ADDR - VIRT_ADDR)

typedef struct memory_desc memory_desc_t;

struct memory_desc {
    memory_desc_t *next, *prev;
    uint32_t size;
    uint8_t *start;
} __attribute__((packed));

extern uint32_t _kernel_end;
memory_desc_t *kernel_avail;
static uint32_t unused_heap_addr;
static memory_desc_t *list;

const int HEAP_MANAGE_PAGES = KHEAP_PAGES * sizeof(memory_desc_t) / PAGE_SIZE;

void mm_init(void) {
    kernel_avail = (memory_desc_t*)&_kernel_end;

    void *phys = alloc_frames(HEAP_MANAGE_PAGES);
    map_pages(phys, (void*)kernel_avail, 0x3, HEAP_MANAGE_PAGES);

    list = kernel_avail;
    memset(kernel_avail, 0, HEAP_MANAGE_PAGES * sizeof(memory_desc_t));

    kernel_avail[0].prev = 0;
    kernel_avail[0].next = 0;
    kernel_avail[0].size = 1;
    kernel_avail[0].start = (uint8_t*)KHEAP_MAX_ADDR;

    unused_heap_addr = KHEAP_MAX_ADDR - PAGE_SIZE;

    printf("Kernel virtual address allocation enabled\n");
}

void* kvirtual_alloc(unsigned int num_pages) {
    memory_desc_t *mem_addr = list;
    void *ptr = NULL;

    while(mem_addr) {
        if(mem_addr->size >= num_pages) {
            ptr = (void*)(mem_addr->start);

            if(mem_addr->size == num_pages) {
                if(mem_addr->prev)
                    mem_addr->prev->next = mem_addr->next;
                if(mem_addr->next)
                    mem_addr->next->prev = mem_addr->prev;
            }

            else if(mem_addr->size > num_pages) {
                uint32_t temp = (uint32_t)ptr + num_pages * PAGE_SIZE;
                int index = get_index(temp);

                memory_desc_t *new_desc = &kernel_avail[index];
                new_desc->size = mem_addr->size - num_pages;

                if(mem_addr->prev)
                    mem_addr->prev->next = new_desc;
                new_desc->prev = mem_addr->prev;
                new_desc->next = mem_addr->next;
                if(mem_addr->next)
                    mem_addr->next->prev = new_desc;
            }

            if(list == mem_addr)
                list = mem_addr->next;

            memset(mem_addr, 0, sizeof(memory_desc_t));
            void *phys = alloc_frame(1);
            map_page(phys, ptr, 0x3);

            return ptr;
        }

        mem_addr = mem_addr->next;
    }

    if(unused_heap_addr >= KHEAP_START_ADDR)
        ptr = (void*)unused_heap_addr;
    else 
        kerror("KERNEL OUT OF VIRTUAL MEMORY");

    unused_heap_addr = (uint32_t)unused_heap_addr - num_pages * PAGE_SIZE;
    void *phys = alloc_frame(1);
    map_page(phys, ptr, 0x3);

    return ptr;
}

void kvirtual_free(void* addr, unsigned int num_pages) {

}
