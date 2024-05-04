// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <string.h>
#include <lilac/panic.h>
#include <lilac/elf.h>
#include "paging.h"
#include "pgframe.h"

void* elf32_load(void *elf)
{
    struct elf_header *hdr = (struct elf_header*)elf;

    if (hdr->sig != 0x464c457f) {
        printf("Invalid ELF signature\n");
        return 0;
    }

    if (hdr->elf32.mach != X86) {
        printf("Invalid machine type\n");
        return 0;
    }

    printf("ELF header:\n");
    printf("\tELF sig: %x\n", hdr->sig);
    printf("\tClass: %x\n", hdr->class);
    printf("\tEndian: %x\n", hdr->endian);
    printf("\tHeader version: %x\n", hdr->headv);
    printf("\tABI: %x\n", hdr->abi);
    printf("\tType: %x\n", hdr->elf32.type);
    printf("\tMachine: %x\n", hdr->elf32.mach);
    printf("\tVersion: %x\n", hdr->elf32.elfv);
    printf("\tEntry: %x\n", hdr->elf32.entry);
    printf("\tProgram header table: %x\n", hdr->elf32.p_tbl);
    printf("\tProgram header table entry size: %x\n", hdr->elf32.p_entry_sz);
    printf("\tProgram header table entry count: %x\n", hdr->elf32.p_tbl_sz);

    struct elf32_pheader *phdr = (struct elf32_pheader*)((u32)elf + hdr->elf32.p_tbl);
    // printf("Program header table:\n");
    // for (int i = 0; i < hdr->elf32.p_tbl_sz; i++) {
    //     printf("Type: %x\n", phdr[i].type);
    //     printf("Offset: %x\n", phdr[i].p_offset);
    //     printf("Virt addr: %x\n", phdr[i].p_vaddr);
    //     printf("File size: %x\n", phdr[i].p_filesz);
    //     printf("Mem size: %x\n", phdr[i].p_memsz);
    //     printf("Flags: %x\n", phdr[i].flags);
    //     printf("Align: %x\n", phdr[i].align);
    // }


    for (int i = 0; i < hdr->elf32.p_tbl_sz; i++) {
        if (phdr[i].type != LOAD_SEG)
            continue;
        if (phdr[i].align > PAGE_BYTES)
            kerror("Alignment greater than page size\n");
        int num_pages = phdr[i].p_memsz / PAGE_BYTES + 1;
        int flags = PG_USER;

        void *phys = alloc_frames(num_pages);
	    void *vaddr = (void*)(phdr[i].p_vaddr & 0xFFFFF000);

        map_pages(phys, vaddr, PG_USER | PG_WRITE, num_pages);

	    memcpy(vaddr, (u8*)elf + phdr[i].p_offset, phdr[i].p_memsz);
        if (phdr[i].p_filesz < phdr[i].p_memsz)
            memset((void*)(phdr[i].p_vaddr + phdr[i].p_filesz), 0,
                    phdr[i].p_memsz - phdr[i].p_filesz);

        if (phdr[i].flags & WRIT)
            flags |= PG_WRITE;
        unmap_pages(vaddr, num_pages);
        map_pages(phys, vaddr, flags, num_pages);
    }

    return (void*)hdr->elf32.entry;
}
