#include <asm/io.h>
#include <lilac/port.h>
#include <lilac/panic.h>

#define COM2 0x2F8

void serial_init(void)
{
    outb(COM2 + 1, 0x00); // disable interrupts
    outb(COM2 + 3, 0x80); // DLAB on
    outb(COM2 + 0, 0x01); // divisor low (115200)
    outb(COM2 + 1, 0x00); // divisor high
    outb(COM2 + 3, 0x03); // 8n1
    outb(COM2 + 2, 0xC7); // FIFO
    outb(COM2 + 4, 0x0B); // IRQs, RTS/DSR
/*
    i8042_wait_input();
    outb(0x64, 0xAD);   // disable keyboard

    i8042_wait_input();
    outb(0x64, 0xA7);   // disable mouse

    while (inb(0x64) & 0x01) {
        inb(0x60);
    }

    i8042_wait_input();
    outb(0x64, 0xAA); // self test

    i8042_wait_output();
    u8 resp = inb(0x60);

    if (resp != 0x55) {
        klog(LOG_ERROR, "PS/2 controller self-test failed (response: 0x%02x)\n", resp);
        return;
    }

    i8042_wait_input();
    outb(0x64, 0x20);
    i8042_wait_output();
    u8 cfg = inb(0x60);
    klog(LOG_INFO, "PS/2 controller config: 0x%02x\n", cfg);

    cfg |= 0x01;     // enable IRQ1
    cfg &= ~(1 << 6); // disable translation

    i8042_wait_input();
    outb(0x64, 0x60);

    i8042_wait_input();
    outb(0x60, cfg);

    i8042_wait_input();
    outb(0x64, 0xAE);  // enable keyboard port

    i8042_wait_input();
    outb(0x64, 0x20);
    i8042_wait_output();
    cfg = inb(0x60);
    klog(LOG_INFO, "New PS/2 controller config: 0x%02x\n", cfg);
*/
}

u32 read_port(u32 Address, u32 Width)
{
    switch (Width) {
        case 8:
            return inb(Address);
        case 16:
            return inw(Address);
        case 32:
            return inl(Address);
    }
    return 0;
}

int write_port(u32 Address, u32 Value, u32 Width)
{
    switch (Width) {
        case 8:
            outb(Address, Value);
            break;
        case 16:
            outw(Address, Value);
            break;
        case 32:
            outl(Address, Value);
            break;
        default:
            return -1;
    }
    return 0;
}

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
