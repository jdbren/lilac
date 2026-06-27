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
    PHDR_SEG    = 0x06,
};

enum segment_flags {
    EXEC = 1,
    WRIT = 2,
    READ = 4,
};

struct __packed elf32_header {
    u16 type;
    u16 mach;
    u32 elfv;
    u32 entry;
    u32 p_tbl_off;
    u32 s_tbl_off;
    u32 flags;
    u16 header_sz;
    u16 p_entry_sz;
    u16 p_tbl_num_ents;
    u16 s_entry_sz;
    u16 s_tbl_num_ents;
    u16 s_idx_header_strs;
};

struct __packed elf64_header {
    u16 type;
    u16 mach;
    u32 elfv;
    u64 entry;
    u64 p_tbl_off;
    u64 s_tbl_off;
    u32 flags;
    u16 header_sz;
    u16 p_entry_sz;
    u16 p_tbl_num_ents;
    u16 s_entry_sz;
    u16 s_tbl_num_ents;
    u16 s_idx_header_strs;
};

struct __packed elf_header {
    u32 sig;
    u8 class;
    u8 endian;
    u8 header_ver;
    u8 abi;
    u8 padding[8];
    union {
        struct elf32_header elf32;
        struct elf64_header elf64;
    };
};

struct __packed elf32_pheader {
    u32 type;
    u32 p_offset;
    u32 p_vaddr;
    u32 paddr;
    u32 p_filesz;
    u32 p_memsz;
    u32 flags;
    u32 align;
};

struct __packed elf64_pheader {
    u32 type;
    u32 flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 align;
};

struct mm_info;
struct file;

/*
 * Output from elf_load(). Carries everything process setup needs to
 * build the initial auxv and choose the correct entry point.
 */
struct exec_info {
    void      *entry;        /* jump target: interp entry, or app entry if static */
    void      *app_entry;    /* original app e_entry  → AT_ENTRY                  */
    void      *at_phdr;      /* user-VA of main phdrs → AT_PHDR                   */
    u16        phnum;        /* phdr count            → AT_PHNUM                  */
    u16        phentsize;    /* phdr entry size       → AT_PHENT                  */
    uintptr_t  interp_base;  /* interp load bias      → AT_BASE  (0 if static)    */
};

/* AT_* auxiliary-vector type constants */
#define AT_NULL         0
#define AT_PHDR         3
#define AT_PHENT        4
#define AT_PHNUM        5
#define AT_PAGESZ       6
#define AT_BASE         7
#define AT_ENTRY        9
#define AT_UID          11
#define AT_EUID         12
#define AT_GID          13
#define AT_EGID         14
#define AT_SECURE       23
#define AT_RANDOM       25
#define AT_EXECFN       31

int elf_load(struct file *elf_file, struct mm_info *mm, struct exec_info *info);

#endif
