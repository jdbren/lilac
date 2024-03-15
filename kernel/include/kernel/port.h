#ifndef _KERNEL_IO_H
#define _KERNEL_IO_H

#include <kernel/types.h>

u32 ReadPort(u32 Address, u32 Width);
int WritePort(u32 Address, u32 Value, u32 Width);

#endif
