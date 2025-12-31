#ifndef _ARCH_I386_IO_H
#define _ARCH_I386_IO_H

#ifndef __ASSEMBLY__

#include <lilac/types.h>

void serial_init(void);

void outb(u16 port, u8 value);
void outw(u16 port, u16 value);
void outl(u16 port, u32 value);
u8   inb(u16 port);
u16  inw(u16 port);
u32  inl(u16 port);
void io_wait(void);

static inline void i8042_wait_input(void)
{
    while (inb(0x64) & 0x02)
        ; // wait for IBF=0
}

static inline void i8042_wait_output(void)
{
    while (!(inb(0x64) & 0x01))
        ; // wait for OBF=1
}


#endif /* __ASSEMBLY__ */

#endif
