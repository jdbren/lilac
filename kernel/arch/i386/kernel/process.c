#include <string.h>
#include <kernel/process.h>
#include <kernel/types.h>
#include <kernel/panic.h>
#include <kernel/config.h>
#include <mm/kmm.h>
#include "pgframe.h"
#include "paging.h"

#define PG_DIR_INDEX(x) (((x) >> 22) & 0x3FF)
#define PG_TABLE_INDEX(x) (((x) >> 12) & 0x3FF)

extern const u32 _kernel_end;
static const int KERNEL_FIRST_PT = PG_DIR_INDEX(__KERNEL_BASE);

static u32 *pd = (u32*)0xFFFFF000;

struct mm_info arch_process_mmap()
{
    u32 *page_dir = kvirtual_alloc(0x1000, PG_WRITE | PG_SUPER);
    u32 cr3 = (u32)get_physaddr(page_dir);

    // Do recursive mapping
    page_dir[1023] = cr3 | PG_WRITE | 0x1;
    printf("page_dir[1023]: %x\n", page_dir[1023]);

    // map io space
    page_dir[0] = pd[0];

    // Allocate kernel stack
    void *kstack = kvirtual_alloc(__KERNEL_STACK_SZ, PG_WRITE);

    // Map kernel space
    const int KERNEL_LAST_PT = PG_DIR_INDEX(_kernel_end);
    for (int i = KERNEL_FIRST_PT; i < 1023; i++)
        page_dir[i] = pd[i];

    struct mm_info info = {cr3, kstack};

    return info;
}
