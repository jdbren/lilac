#ifndef _USER_H
#define _USER_H

#include <lilac/lilac.h>
#include <lilac/sched.h>
#include <mm/mm.h>

extern int arch_user_copy(void *dst, const void *src, size_t size);
extern int arch_strncpy_from_user(char *dst, const char *src, size_t max_size);
extern int arch_strnlen_user(const char *str, size_t max_size);
extern int arch_cmpxchg4_user(int __user *ptr, int oldval, int newval,
                              int *uvalptr);
extern int arch_cmpxchg8_user(long long __user *ptr, long long oldval,
                              long long newval, long long *uvalptr);

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

/**
 * Puts a simple value into user space. Returns 0 on success, -EFAULT on error.
 */
#define put_user(x, ptr) ({ \
    __typeof__(*(ptr)) __put_user_val = (x); \
    __put_user(&__put_user_val, (ptr), sizeof(__put_user_val)); \
})

/**
 * Gets a simple value from user space. Returns 0 on success, -EFAULT on error.
 */
#define get_user(x, ptr) ({ \
    __typeof__((x)) __get_user_val; \
    int __get_user_err = copy_from_user(&__get_user_val, (ptr), sizeof(__get_user_val)); \
    if (__get_user_err == 0) \
        (x) = __get_user_val; \
    __get_user_err; \
})

/**
 * Atomically compare the 4 byte value at ptr with oldval.
 * If they match, replace it with newval.
 * The value at ptr is stored in *uvalptr.
 * Returns 1 if values match or 0 if not. -EFAULT on error.
 */
__must_check static inline int atomic_cmpxchg4_user(int __user *ptr,
    int oldval, int newval, int *uvalptr)
{
    return arch_cmpxchg4_user(ptr, oldval, newval, uvalptr);
}

/**
 * Atomically compare the 8 byte value at ptr with oldval.
 * If they match, replace it with newval.
 * The value at ptr is stored in *uvalptr.
 * Returns 1 if values match or 0 if not. -EFAULT on error.
 */
__must_check static inline int atomic_cmpxchg8_user(long long __user *ptr,
    long long oldval, long long newval, long long *uvalptr)
{
    return arch_cmpxchg8_user(ptr, oldval, newval, uvalptr);
}

#endif
