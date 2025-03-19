// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _KERNEL_CONFIG_H
#define _KERNEL_CONFIG_H

#define __lilac__   1
#define _LILAC      1

#define PAGE_SIZE           4096

#ifdef ARCH_x86_64
#define __PHYS_MAP_ADDR     0xffff900000000000ULL
#define KHEAP_START_ADDR    0xfffffffa00000000ULL
#define KHEAP_MAX_ADDR      0xfffffffb00000000ULL
#define __KERNEL_BASE       0xffffffff80000000ULL
#define __KERNEL_MAX_ADDR   0xffffffff80200000ULL
#define __USER_STACK        0x0000800000000000ULL
#define __USER_BRK          0x0000400000000000ULL
#else
#define __KERNEL_BASE       0xC0000000
#define __KERNEL_MAX_ADDR   0xC0400000
#define KHEAP_START_ADDR    0xC0400000
#define KHEAP_MAX_ADDR      0xEFFFF000
#define __USER_STACK        0x80000000
#define __USER_BRK          0x40000000
#endif

#define __KERNEL_STACK_SZ   0x2000
#define __USER_STACK_SZ     (PAGE_SIZE * 32)

#define pa(X) ((X) - __KERNEL_BASE)

#define __packed        __attribute__((packed))
#define __align(x)      __attribute__((aligned(x)))
#define __must_check    __attribute__((__warn_unused_result__))
#define __noreturn      __attribute__((noreturn))

#define __cacheline_align __attribute__((aligned(64)))

#define unreachable() __builtin_unreachable()

#define is_aligned(POINTER, BYTE_COUNT) \
    (((uintptr_t)(POINTER)) % (BYTE_COUNT) == 0)

#define is_canonical(addr) \
    (((addr) < 0x0000ffffffffffffUL) || \
     ((addr) > 0xffff000000000000UL))

#endif
