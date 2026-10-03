#include <lilac/types.h>
#include <lilac/log.h>

int arch_user_copy(void *dst, const void *src, size_t size)
{
    int ret;
    asm volatile (
        "call x86_user_copy_asm\n\t"
        :"=a"(ret), "+D"(dst), "+S"(src), "+c"(size)
        :
        :"memory", "cc"
    );
    return ret;
}

int arch_strncpy_from_user(char *dst, const char *src, size_t max_size)
{
    int ret;
    asm volatile (
        "call x86_strncpy_from_user_asm\n\t"
        :"=a"(ret), "+D"(dst), "+S"(src), "+c"(max_size)
        :
#ifdef __x86_64__
        :"memory", "cc", "r8"
#else
        :"memory", "cc", "edx"
#endif
    );
    return ret;
}

int arch_strnlen_user(const char *str, size_t max_size)
{
    int ret;
    asm volatile (
        "call x86_strnlen_user_asm\n\t"
        :"=a"(ret), "+D"(str)
        :"S"(max_size)
        :"memory", "cc", "rcx"
    );
    return ret;
}
