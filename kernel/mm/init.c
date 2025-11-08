#include <lilac/mm.h>
#include <lilac/kmm.h>
#include <lilac/pmem.h>

void mm_init(void)
{
    pmem_init();
    kernel_pt_init(KHEAP_START_ADDR, KHEAP_MAX_ADDR);
}
