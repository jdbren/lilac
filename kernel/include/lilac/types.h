// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#pragma once
#ifndef _LILAC_TYPES_H
#define _LILAC_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef signed long ssize_t;
typedef unsigned short umode_t;
typedef u32 dev_t;

#define BITS_PER_LONG 32

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

struct list_head {
    struct list_head *next, *prev;
};

struct hlist_head {
	struct hlist_node *first;
};

struct hlist_node {
	struct hlist_node *next, **pprev;
};

enum disk_cmd {
    DISK_READ,
    DISK_WRITE,
    DISK_FLUSH
};

#endif
