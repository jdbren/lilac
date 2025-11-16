#include <mm/mm.h>
#include <mm/kmm.h>
#include <mm/page.h>

void mm_init(void)
{
    pmem_init();
    kernel_pt_init(KHEAP_START_ADDR, KHEAP_MAX_ADDR);
}
