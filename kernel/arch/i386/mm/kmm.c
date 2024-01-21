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

static void __update_list(memory_desc_t *mem_addr, int num_pages);

extern uint32_t _kernel_end;
static memory_desc_t *kernel_avail;
static memory_desc_t *list;
static uint32_t unused_heap_addr;

static const int HEAP_MANAGE_PAGES = KHEAP_PAGES * sizeof(memory_desc_t) / PAGE_SIZE;

void mm_init(void) 
{
    kernel_avail = (memory_desc_t*)&_kernel_end;
    list = 0;
    unused_heap_addr = KHEAP_MAX_ADDR;

    void *phys = alloc_frames(HEAP_MANAGE_PAGES);
    map_pages(phys, (void*)kernel_avail, 0x3, HEAP_MANAGE_PAGES);
    memset(kernel_avail, 0, HEAP_MANAGE_PAGES * sizeof(memory_desc_t));

    printf("Kernel virtual address allocation enabled\n");
}

void* kvirtual_alloc(unsigned int num_pages) 
{
    memory_desc_t *mem_addr = list;
    void *ptr = NULL;

    while (mem_addr) {
        printf("mem_addr->start: %x\n", mem_addr->start);
        if (mem_addr->size >= num_pages) {
            ptr = (void*)(mem_addr->start);
            __update_list(mem_addr, num_pages);

            void *phys = alloc_frames(num_pages);
            map_pages(phys, ptr, 0x3, num_pages);

            return ptr;
        }

        mem_addr = mem_addr->next;
    }

    if (unused_heap_addr >= KHEAP_START_ADDR)
        ptr = (void*)unused_heap_addr - (num_pages - 1) * PAGE_SIZE;
    else 
        kerror("KERNEL OUT OF VIRTUAL MEMORY");

    unused_heap_addr = (uint32_t)unused_heap_addr - num_pages * PAGE_SIZE;
    void *phys = alloc_frames(num_pages);
    map_pages(phys, ptr, 0x3, num_pages);

    return ptr;
}

void kvirtual_free(void* addr, unsigned int num_pages) 
{
    printf("Freeing %x\n", addr);

    memory_desc_t *mem_addr = &kernel_avail[get_index((uint32_t)addr)];
    mem_addr->start = (uint8_t*)addr;
    mem_addr->size = num_pages;

    if (list) {
        list->prev = mem_addr;
        mem_addr->next = list;
    }

    list = mem_addr;

    free_frames(get_physaddr(addr), num_pages);
    unmap_pages(addr, num_pages);
}

static void __update_list(memory_desc_t *mem_addr, int num_pages) 
{
    void *ptr = (void*)(mem_addr->start);

    if (mem_addr->size > num_pages) {
        uint32_t tmp = (uint32_t)ptr + num_pages * PAGE_SIZE;
        printf("tmp: %x\n", tmp);

        memory_desc_t *new_desc = &kernel_avail[get_index(tmp)];
        new_desc->start = (uint8_t*)tmp;
        new_desc->size = mem_addr->size - num_pages;
        new_desc->prev = mem_addr->prev;
        new_desc->next = mem_addr->next;

        if (mem_addr->prev)
            mem_addr->prev->next = new_desc;
        if (mem_addr->next)
            mem_addr->next->prev = new_desc;
        mem_addr->next = new_desc;
    } else {
        if (mem_addr->prev)
            mem_addr->prev->next = mem_addr->next;
        if (mem_addr->next)
            mem_addr->next->prev = mem_addr->prev;
    }

    if (list == mem_addr)
        list = mem_addr->next;

    memset(mem_addr, 0, sizeof(memory_desc_t));
}
