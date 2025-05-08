#ifndef _KERNEL_IO_H
#define _KERNEL_IO_H

#include <lilac/types.h>

u32 read_port(u32 Address, u32 Width);
int write_port(u32 Address, u32 Value, u32 Width);

#endif
