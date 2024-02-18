#ifndef _KERNEL_PROCESS_H
#define _KERNEL_PROCESS_H

#include <kernel/types.h>

void create_process();
u32 arch_process_mmap();

#endif
