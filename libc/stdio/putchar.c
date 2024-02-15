#include <stdio.h>

#if defined(__is_libk)
#include <kernel/tty.h>
#include "/Users/jbrennem/Classes/Personal/lilac/kernel/arch/i386/include/io.h"
#endif

int putchar(int ic) {
#if defined(__is_libk)
	char c = (char) ic;
	outb(0xe9, c);
	terminal_write(&c, sizeof(c));
#else
	// TODO: Implement stdio and the write system call.
#endif
	return ic;
}
