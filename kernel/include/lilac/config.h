// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _KERNEL_CONFIG_H
#define _KERNEL_CONFIG_H

#define __lilac__   1
#define _LILAC      1

#define PAGE_SIZE           4096

#define __KERNEL_BASE       0xC0000000
#define __KERNEL_MAX_ADDR   0xC8000000
//#define __KERNEL_STACK      __KERNEL_MAX_ADDR
#define __KERNEL_STACK_SZ   0x2000

#define __USER_STACK        0x80000000
#define __USER_STACK_SZ     (PAGE_SIZE * 32)

#define pa(X) ((X) - __KERNEL_BASE)

#define __align(x)      __attribute__((aligned(x)))
#define __must_check    __attribute__((__warn_unused_result__))

#endif
