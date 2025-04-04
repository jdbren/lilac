#include <stddef.h>

void *memmove(void *dstptr, const void *srcptr, size_t size)
{
	unsigned char *dst = (unsigned char*)dstptr;
	const unsigned char *src = (const unsigned char*)srcptr;
	size_t i;
	if (dst < src) {
		for (i = 0; i < size; i++)
			dst[i] = src[i];
	} else {
		for (i = size; i != 0; i--)
			dst[i-1] = src[i-1];
	}
	return dstptr;
}
