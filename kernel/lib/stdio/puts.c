#include <lilac/libc.h>

int puts(const char *string)
{
	return printf("%s\n", string);
}
