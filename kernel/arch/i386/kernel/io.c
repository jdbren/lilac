#ifndef x86_IO_H
#define x86_IO_H

#include <kernel/types.h>

void outb(u16 port, u8 val)
{
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

void outw(u16 port, u16 val)
{
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

void outl(u16 port, u32 val)
{
    asm volatile ("outl %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

u8 inb(u16 port)
{
    u8 ret;
    asm volatile ("inb %1, %0"
                   : "=a"(ret)
                   : "Nd"(port)
                   : "memory");
    return ret;
}

u16 inw(u16 port)
{
    u16 ret;
    asm volatile ("inw %1, %0"
                   : "=a"(ret)
                   : "Nd"(port)
                   : "memory");
    return ret;
}

u32 inl(u16 port)
{
    u32 ret;
    asm volatile ("inl %1, %0"
                   : "=a"(ret)
                   : "Nd"(port)
                   : "memory");
    return ret;
}

void io_wait(void)
{
    asm volatile ("outb %%al, $0x80" : : "a"(0) );
}

#endif