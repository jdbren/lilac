// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#include <lilac/config.h>
#include <lilac/log.h>
#include <lilac/libc.h>

#ifdef __cplusplus
extern "C" {
#endif

void print_bitmap(void);

#ifdef DEBUG
#define assert(x) if (unlikely(!(x))) kerror("Assertion failed: " #x)
#else
#define assert(x) ((void)0)
#endif

__noreturn void kerror(const char *msg, ...);
#define panic(msg, ...) kerror(msg __VA_OPT__(,) __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
