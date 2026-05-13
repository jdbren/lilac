#ifndef _USER_H
#define _USER_H

#include <lilac/lilac.h>
#include <lilac/sched.h>
#include <mm/mm.h>

extern int arch_user_copy(void *dst, const void *src, size_t size);
extern int arch_strncpy_from_user(char *dst, const char *src, size_t max_size);
extern int arch_strnlen_user(const char *str, size_t max_size);

#define __is_code(addr, size, mm) \
    ((uintptr_t)(addr) >= mm->start_code && (uintptr_t)(addr) + size < mm->end_code)
#define __is_stack(addr, size, mm) \
    ((uintptr_t)(addr) >= mm->start_stack && (uintptr_t)(addr) + size < __USER_STACK)
#define __is_data(addr, size, mm) \
    ((uintptr_t)(addr) >= mm->start_data && (uintptr_t)(addr) + size < mm->end_data)
#define __is_brk(addr, size, mm) \
    ((uintptr_t)(addr) >= mm->start_brk && (uintptr_t)(addr) + size < mm->brk)

#define __access_ok(addr, size, mm) (addr && \
    ((uintptr_t)addr + size <= __USER_MAX_ADDR) && \
    (__is_data(addr, size, mm) || __is_stack(addr, size, mm) || \
    __is_brk(addr, size, mm) || __is_code(addr, size, mm)))

#define access_ok(addr, size) ({ \
    struct mm_info *mm = current->mm; \
    __access_ok(addr, size, mm) || (find_vma(mm, (uintptr_t)addr) != NULL); \
})

/**
 * Copy size bytes to user space. Returns 0 on success
 */
static __must_check inline
int copy_to_user(void *dst, const void *src, size_t size)
{
    if (!access_ok(dst, size)) {
        klog(LOG_WARN, "copy_to_user: dst not accessible: %p\n", dst);
        return -EFAULT;
    }
    return arch_user_copy(dst, src, size);
}

/**
 * Copy size bytes from user space. Returns 0 on success
 */
static __must_check inline
int copy_from_user(void *dst, const void *src, size_t size)
{
    if (!access_ok(src, size)) {
        klog(LOG_WARN, "copy_from_user: src not accessible: %p\n", src);
        return -EFAULT;
    }
    return arch_user_copy(dst, src, size);
}

/**
 * Copies a NUL-terminated string from user space to kernel space.
 * Returns the number of bytes copied.
 */
static __must_check inline
int strncpy_from_user(char *dst, const char *src, size_t max_size)
{
    if (!access_ok(src, 1)) {
        klog(LOG_WARN, "strncpy_from_user: src (%p) not accessible\n", src);
        return -EFAULT;
    }
    return arch_strncpy_from_user(dst, src, max_size);
}

static inline
int strncpy_to_user(char *dst, const char *src, size_t max_size)
{
    if (!access_ok(dst, 1)) {
        klog(LOG_WARN, "strncpy_to_user: dst (%p) not accessible\n", dst);
        return -EFAULT;
    }
    size_t len = strnlen(src, max_size);
    if (len >= max_size) {
        klog(LOG_WARN, "strncpy_to_user: string too long\n");
        return -ENAMETOOLONG;
    }
    return copy_to_user(dst, src, len + 1);
}

static __must_check inline
int strnlen_user(const char *str, int max)
{
    if (!access_ok(str, 1)) {
        klog(LOG_WARN, "strnlen_user: str not accessible: %p\n", str);
        return -EFAULT;
    }
    return arch_strnlen_user(str, max);
}

static __must_check inline
int user_str_ok(const char *str, int max_size)
{
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

static __always_inline
int __put_user(const void *src, __user void *ptr, size_t size)
{
    return copy_to_user(ptr, src, size);
}

#define put_user(x, ptr) ({ \
    __typeof__(*(ptr)) __put_user_val = (x); \
    __put_user(&__put_user_val, (ptr), sizeof(__put_user_val)); \
})

#define get_user(x, ptr) ({ \
    __typeof__((x)) __get_user_val; \
    int __get_user_err = copy_from_user(&__get_user_val, (ptr), sizeof(__get_user_val)); \
    if (__get_user_err == 0) \
        (x) = __get_user_val; \
    __get_user_err; \
})

#endif
