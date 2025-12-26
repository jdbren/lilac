#include <lilac/console.h>
#include <lilac/port.h>

#define LSR_THRE 0x20   // Transmit Holding Register Empty
#define COM1 0x3F8
#define COM2 0x2F8

static inline void serial_putc(char c)
{
    while ((read_port(COM2 + 5, 8) & LSR_THRE) == 0)
        ;

    write_port(COM2, c, 8);
}

int putchar(int ic)
{
    char c = (char)ic;
    if (c == '\n')
        serial_putc('\r');
    serial_putc(c);
    write_port(0xe9, c, 8); // Bochs debug port
    if (write_to_screen)
        console_write(NULL, &c, 1);
    return ic;
}
