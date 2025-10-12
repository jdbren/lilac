#ifndef _USER_H
#define _USER_H

#include <lilac/lilac.h>
#include <lilac/sched.h>

extern int arch_user_copy(void *dst, const void *src, size_t size);
extern int arch_strncpy_from_user(char *dst, const char *src, size_t max_size);
extern int arch_strnlen_user(const char *str, size_t max_size);

#define _is_code(addr, size, mm) \
    ((uintptr_t)(addr) >= mm->start_code && (uintptr_t)(addr) + size < mm->end_code)
#define _is_stack(addr, size, mm) \
    ((uintptr_t)(addr) >= mm->start_stack && (uintptr_t)(addr) + size < __USER_STACK)
#define _is_data(addr, size, mm) \
    ((uintptr_t)(addr) >= mm->start_data && (uintptr_t)(addr) + size < mm->end_data)
#define _is_brk(addr, size, mm) \
    ((uintptr_t)(addr) + size < mm->brk)

#define __access_ok(addr, size, mm) (addr && \
    (_is_data(addr, size, mm) || _is_stack(addr, size, mm) || \
    _is_brk(addr, size, mm) || _is_code(addr, size, mm)))

#define access_ok(addr, size) ({ \
    struct mm_info *mm = current->mm; \
    __access_ok(addr, size, mm); \
})

__must_check
static inline int check_access(void *addr, size_t size)
{
    if (!access_ok(addr, size)) {
        return -EFAULT;
    }
    return 0;
}

__must_check
static inline int copy_to_user(void *dst, const void *src, size_t size)
{
    if (!access_ok(dst, size)) {
        klog(LOG_WARN, "copy_to_user: dst not accessible\n");
        return -EFAULT;
    }
    return arch_user_copy(dst, src, size);
}

__must_check
static inline int copy_from_user(void *dst, const void *src, size_t size)
{
    if (!access_ok(src, size)) {
        klog(LOG_WARN, "copy_from_user: src not accessible\n");
        return -EFAULT;
    }
    return arch_user_copy(dst, src, size);
}

/**
 * Copies a NUL-terminated string from user space to kernel space.
 * Returns the number of bytes copied.
 */
static __must_check inline int strncpy_from_user(char *dst, const char *src, size_t max_size)
{
    if (!access_ok(src, 1)) {
        klog(LOG_WARN, "strncpy_from_user: src (%x) not accessible\n", src);
        return -EFAULT;
    }
    return arch_strncpy_from_user(dst, src, max_size);
}

__must_check
static inline int strnlen_user(const char *str, int max)
{
    if (!access_ok(str, 1)) {
        klog(LOG_WARN, "strnlen_user: str not accessible\n");
        return -EFAULT;
    }
    return arch_strnlen_user(str, max);
}

__must_check
static inline int user_str_ok(const char *str, int max_size)
{
    struct mm_info *mm = current->mm;
    int len = strnlen_user(str, max_size);
    if (len < 0) {
        klog(LOG_WARN, "user_str_ok: str (%x) not accessible\n", str);
        return -EFAULT; // Error in accessing the string
    } else if (len >= max_size) {
        klog(LOG_WARN, "user_str_ok: string too long\n");
        return -ENAMETOOLONG;
    }
    return 0;
}

#endif
