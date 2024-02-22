// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <string.h>
#include <kernel/panic.h>
#include <kernel/elf.h>
#include "paging.h"
#include "pgframe.h"

void* elf32_load(void *elf)
{
    struct elf_header *hdr = (struct elf_header*)elf;

    if (hdr->sig != 0x464c457f) {
        printf("Invalid ELF signature\n");
        return 0;
    }

    printf("ELF sig: %x\n", hdr->sig);
    printf("Class: %x\n", hdr->class);
    printf("Endian: %x\n", hdr->endian);
    printf("Header version: %x\n", hdr->headv);
    printf("ABI: %x\n", hdr->abi);
    printf("Type: %x\n", hdr->elf32.type);
    printf("Machine: %x\n", hdr->elf32.mach);
    printf("Version: %x\n", hdr->elf32.elfv);
    printf("Entry: %x\n", hdr->elf32.entry);
    printf("Program header table: %x\n", hdr->elf32.p_tbl);
    printf("Program header table entry size: %x\n", hdr->elf32.p_entry_sz);
    printf("Program header table entry count: %x\n", hdr->elf32.p_tbl_sz);

    struct elf32_pheader *phdr = (struct elf32_pheader*)((u32)elf + hdr->elf32.p_tbl);
    printf("Program header table:\n");
    for (int i = 0; i < hdr->elf32.p_tbl_sz; i++) {
        printf("Type: %x\n", phdr[i].type);
        printf("Offset: %x\n", phdr[i].p_offset);
        printf("Virt addr: %x\n", phdr[i].p_vaddr);
        printf("File size: %x\n", phdr[i].p_filesz);
        printf("Mem size: %x\n", phdr[i].p_memsz);
        printf("Flags: %x\n", phdr[i].flags);
        printf("Align: %x\n", phdr[i].align);
    }


    for (int i = 0; i < hdr->elf32.p_tbl_sz; i++) {
        if (phdr[i].align > PAGE_BYTES)
            kerror("Alignment greater than page size\n");
        void *phys = alloc_frame(phdr[i].p_memsz / PAGE_BYTES + 1);
	    void *vaddr = (void*)phdr[i].p_vaddr;
        assert((u32)vaddr % PAGE_BYTES == 0);

        map_page(phys, vaddr, PG_USER | PG_WRITE);
	    memcpy(vaddr, elf, phdr[i].p_memsz);
        if (phdr[i].p_filesz < phdr[i].p_memsz)
            memset((void*)(phdr[i].p_vaddr + phdr[i].p_filesz), 0,
                    phdr[i].p_memsz - phdr[i].p_filesz);
    }


    return (void*)hdr->elf32.entry;
}
