// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#pragma once
#ifndef _KERNEL_TYPES_H
#define _KERNEL_TYPES_H

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

#define BITS_PER_LONG 32

#include <kernel/sync.h>

#endif
