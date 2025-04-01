#include <stddef.h>

void *memcpy(void *restrict dstptr, const void *restrict srcptr, size_t size)
{
	unsigned char *restrict dst = (unsigned char*)dstptr;
	const unsigned char *restrict src = (const unsigned char*)srcptr;
	while (size--)
	 	*dst++ = *src++;
	return dstptr;
}
