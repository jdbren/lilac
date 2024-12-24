// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef KERNEL_ELF_H
#define KERNEL_ELF_H

#include <lilac/types.h>
#include <lilac/config.h>

#define ELF_MAGIC 0x464C457FU

enum ISA {
    X86     = 0x03,
    ARM     = 0x28,
    X86_64  = 0x3E,
    AARCH64 = 0xB7,
};

enum segment_type {
    NULL_SEG    = 0x00,
    LOAD_SEG    = 0x01,
    DYNAMIC_SEG = 0x02,
    INTERP_SEG  = 0x03,
    NOTE_SEG    = 0x04,
};

enum segment_flags {
    EXEC = 1,
    WRIT = 2,
    READ = 4,
};

struct elf32_header {
    u16 type;
    u16 mach;
    u32 elfv;
    u32 entry;
    u32 p_tbl;
    u32 s_tbl;
    u32 flg;
    u16 hd_sz;
    u16 p_entry_sz;
    u16 p_tbl_sz;
    u16 s_entry_sz;
    u16 s_tbl_sz;
    u16 nms_idx;
} __packed;

struct elf64_header {
    u16 type;
    u16 mach;
    u32 elfv;
    u64 entry;
    u64 p_tbl;
    u64 s_tbl;
    u32 flg;
    u16 hd_sz;
    u16 p_entrysz;
    u16 p_tblsz;
    u16 s_entrysz;
    u16 s_tblsz;
    u16 nms_idx;
} __packed;

struct elf_header {
    u32 sig;
    u8 class;
    u8 endian;
    u8 headv;
    u8 abi;
    u8 padding[8];
    union {
        struct elf32_header elf32;
        struct elf64_header elf64;
    };
} __packed;

struct elf32_pheader {
    u32 type;
    u32 p_offset;
    u32 p_vaddr;
    u32 paddr;
    u32 p_filesz;
    u32 p_memsz;
    u32 flags;
    u32 align;
} __packed;

struct elf64_pheader {
    u32 type;
    u32 flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 align;
} __packed;

struct mm_info;

void *elf32_load(void *elf, struct mm_info *mm);

#endif
