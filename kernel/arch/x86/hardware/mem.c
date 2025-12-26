#include <stdlib.h>
#include <stdint.h>

void x86_memcpy_dwords(void *dst, const void *src, size_t size)
{
    void *d = dst;
    const void *s = src;

    while (((uintptr_t)d & 3) && size > 0) {
        *(char*)d++ = *(const char*)s++;
        size--;
    }

    size_t n = size / 4;
    asm volatile (
        "rep movsl"
        : "+D"(d), "+S"(s), "+c"(n)
        : : "memory"
    );

    size_t remaining = size % 4;
    if (remaining) {
        char *d8 = (char*)d;
        const char *s8 = (const char*)s;
        while (remaining--) *d8++ = *s8++;
    }
}

void x86_memcpy_qwords(void *dst, const void *src, size_t size)
{
    void *d = dst;
    const void *s = src;

    while (((uintptr_t)d & 7) && size > 0) {
        *(char*)d++ = *(const char*)s++;
        size--;
    }

    size_t n = size / 8;
    asm volatile (
        "rep movsq"
        : "+D"(d), "+S"(s), "+c"(n)
        : : "memory"
    );

    size_t remaining = size % 8;
    if (remaining) {
        char *d8 = (char*)d;
        const char *s8 = (const char*)s;
        while (remaining--) *d8++ = *s8++;
    }
}

void x86_memcpy_sse_nt(void *dst, const void *src, size_t size)
{
    char *d = dst;
    const char *s = src;

    while (((uintptr_t)d & 15) && size > 0) {
        *d++ = *s++;
        size--;
    }

    // Copy 64 bytes at a time using non-temporal SSE stores
    while (size >= 64) {
        asm volatile (
            "movdqu (%1), %%xmm0\n"
            "movdqu 16(%1), %%xmm1\n"
            "movdqu 32(%1), %%xmm2\n"
            "movdqu 48(%1), %%xmm3\n"
            "movntdq %%xmm0, (%0)\n"
            "movntdq %%xmm1, 16(%0)\n"
            "movntdq %%xmm2, 32(%0)\n"
            "movntdq %%xmm3, 48(%0)\n"
            :
            : "r"(d), "r"(s)
            : "memory"
        );
        d += 64;
        s += 64;
        size -= 64;
    }

    while (size--)
        *d++ = *s++;
}
