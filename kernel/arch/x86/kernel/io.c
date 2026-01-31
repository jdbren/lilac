#include <asm/io.h>
#include <lilac/port.h>
#include <lilac/panic.h>
#include <lilac/tty.h>
#include <asm/idt.h>
#include <asm/apic.h>
#include <asm/segments.h>

#define COM2 0x2F8

extern void serial_handler(void);

void serial_int(void)
{
    u8 lsr = inb(COM2 + 5);
    if (lsr & (1 << 1)) klog(LOG_ERROR, "Overrun error\n");
    if (lsr & (1 << 2)) klog(LOG_ERROR, "Parity error\n");
    if (lsr & (1 << 3)) klog(LOG_ERROR, "Framing error\n");
    while (lsr & 1) {
        tty_recv_char(inb(COM2));
        lsr = inb(COM2 + 5);
    }
}

void serial_irq_init(void)
{
    idt_entry(0x20 + 3, (uintptr_t)serial_handler, __KERNEL_CS, 0, INT_GATE);
    ioapic_entry(3, 0x20 + 3, 0, 0);
    outb(COM2 + 1, 0x01); // enable RX irq
    klog(LOG_INFO, "Serial IRQ initialized\n");
}

void serial_init(void)
{
    outb(COM2 + 1, 0x00); // disable interrupts
    outb(COM2 + 3, 0x80); // DLAB on
    outb(COM2 + 0, 0x01); // divisor low (115200)
    outb(COM2 + 1, 0x00); // divisor high
    outb(COM2 + 3, 0x03); // 8n1
    outb(COM2 + 2, 0x07); // FIFO
    outb(COM2 + 4, 0x0B); // IRQs, RTS/DSR

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
    outb(0x60, cfg); // write config byte

    i8042_wait_input();
    outb(0x64, 0xAE);  // enable keyboard port

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
