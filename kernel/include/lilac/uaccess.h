#ifndef _USER_H
#define _USER_H

#include <lilac/config.h>
#include <lilac/types.h>
#include <lilac/libc.h>
#include <lilac/errno.h>
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

#define access_ok(addr, size, mm) \
    (_is_data(addr, size, mm) || _is_stack(addr, size, mm) || \
    _is_brk(addr, size, mm) || _is_code(addr, size, mm))

static inline __must_check int check_access(void *addr, size_t size)
{
    struct mm_info *mm = current->mm;
    if (!access_ok(addr, size, mm)) {
        return -EFAULT;
    }
    return 0;
}

static inline __must_check int copy_to_user(void *dst, const void *src, size_t size)
{
    struct mm_info *mm = current->mm;
    if (!access_ok(dst, size, mm)) {
        klog(LOG_WARN, "copy_to_user: dst not accessible\n");
        return -EFAULT;
    }
    return arch_user_copy(dst, src, size);
}

static inline __must_check int copy_from_user(void *dst, const void *src, size_t size)
{
    struct mm_info *mm = current->mm;
    if (!access_ok(src, size, mm)) {
        klog(LOG_WARN, "copy_from_user: src not accessible\n");
        return -EFAULT;
    }
    return arch_user_copy(dst, src, size);
}

static inline __must_check int strncpy_from_user(char *dst, const char *src, size_t max_size)
{
    struct mm_info *mm = current->mm;
    if (!access_ok(src, 1, mm)) {
        klog(LOG_WARN, "strncpy_from_user: src (%x) not accessible\n", src);
        return -EFAULT;
    }
    return arch_strncpy_from_user(dst, src, max_size);
}

static inline __must_check int strnlen_user(const char *str, int max)
{
    struct mm_info *mm = current->mm;
    if (!access_ok(str, 1, mm)) {
        klog(LOG_WARN, "strnlen_user: str not accessible\n");
        return -EFAULT;
    }
    return arch_strnlen_user(str, max);
}

static inline __must_check int user_str_ok(const char *str, int max_size)
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
