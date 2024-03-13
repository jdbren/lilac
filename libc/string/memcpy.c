#include <string.h>

void *memcpy(void *restrict dstptr, const void *restrict srcptr, register size_t size) {
	register unsigned char *restrict dst = (unsigned char*) dstptr;
	const register unsigned char *restrict src = (const unsigned char*) srcptr;
	while (size--)
	 	*dst++ = *src++;
	return dstptr;
}
