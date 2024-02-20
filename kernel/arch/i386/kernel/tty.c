#include <stdbool.h>
#include <string.h>
#include <kernel/types.h>
#include <kernel/tty.h>
#include <utility/vga.h>

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;
static u16* const VGA_MEMORY = (u16*)0xB8000;

static size_t terminal_row;
static size_t terminal_column;
static u8 terminal_color;
static u16* terminal_buffer;

void terminal_init(void)
{
	terminal_row = 0;
	terminal_column = 0;
	terminal_color = vga_entry_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
	terminal_buffer = VGA_MEMORY;
	for (size_t y = 0; y < VGA_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			const size_t index = y * VGA_WIDTH + x;
			terminal_buffer[index] = vga_entry(' ', terminal_color);
		}
	}
	terminal_writestring("Terminal initialized\n");
}

void terminal_setcolor(u8 color)
{
	terminal_color = color;
}

void terminal_putentryat(unsigned char c, u8 color, size_t x, size_t y)
{
	const size_t index = y * VGA_WIDTH + x;
	terminal_buffer[index] = vga_entry(c, color);
}

void terminal_scroll()
{
	size_t i, k, j;
	for (k = 1, j = 0; k < VGA_HEIGHT; k++, j++) {
		for (i = 0; i < VGA_WIDTH; i++) {
			terminal_putentryat(terminal_buffer[k * VGA_WIDTH + i],
				terminal_color, i, j);
		}
	}
	for (i = 0; i < VGA_WIDTH; i++)
		terminal_putentryat(' ', terminal_color, i, VGA_HEIGHT-1);
	terminal_row--;
}

void terminal_putchar(char c)
{
	unsigned char uc = c;
	if (c == '\n') {
		terminal_column = 0;
		if (++terminal_row == VGA_HEIGHT)
			terminal_scroll();
		return;
	}
	terminal_putentryat(uc, terminal_color, terminal_column, terminal_row);
	if (++terminal_column == VGA_WIDTH) {
		terminal_column = 0;
		if (++terminal_row == VGA_HEIGHT)
			terminal_scroll();
	}
}

void terminal_write(const char* data, size_t size)
{
	for (size_t i = 0; i < size; i++)
		terminal_putchar(data[i]);
}

void terminal_writestring(const char* data)
{
	terminal_write(data, strlen(data));
}
