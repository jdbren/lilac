#ifndef _ARCH_I386_IO_H
#define _ARCH_I386_IO_H

#include <lilac/types.h>

void serial_init(void);

void outb(u16 port, u8 value);
void outw(u16 port, u16 value);
void outl(u16 port, u32 value);
u8   inb(u16 port);
u16  inw(u16 port);
u32  inl(u16 port);
void io_wait(void);

#endif
