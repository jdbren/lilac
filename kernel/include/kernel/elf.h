#ifndef KERNEL_ELF_H
#define KERNEL_ELF_H

#include <kernel/types.h>

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

struct elf32_header;
struct elf64_header;

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
} __attribute__((packed));

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
} __attribute__((packed));

struct elf64_header {
    u16 type;
    u16 mach;
    u32 elfv;
    uint64_t entry;
    uint64_t p_tbl;
    uint64_t s_tbl;
    u32 flg;
    u16 hd_sz;
    u16 p_entrysz;
    u16 p_tblsz;
    u16 s_entrysz;
    u16 s_tblsz;
    u16 nms_idx;
} __attribute__((packed));

struct elf32_pheader {
    u32 type;
    u32 p_offset;
    u32 p_vaddr;
    u32 paddr;
    u32 p_filesz;
    u32 p_memsz;
    u32 flags;
    u32 align;
} __attribute__((packed));

struct elf64_pheader {
    u32 type;
    u32 flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t align;
} __attribute__((packed));

int elf32_load(void *elf, void *dest);

#endif
