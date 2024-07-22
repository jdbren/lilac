// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/elf.h>

#include <string.h>

#include <lilac/panic.h>
#include <lilac/log.h>
#include <lilac/process.h>
#include <lilac/mm.h>
#include <mm/kmalloc.h>
#include "paging.h"
#include "pgframe.h"

void elf_print(struct elf_header *hdr)
{
    klog(LOG_DEBUG, "ELF header:\n");
    klog(LOG_DEBUG, "\tELF sig: %x\n", hdr->sig);
    klog(LOG_DEBUG, "\tClass: %x\n", hdr->class);
    klog(LOG_DEBUG, "\tEndian: %x\n", hdr->endian);
    klog(LOG_DEBUG, "\tHeader version: %x\n", hdr->headv);
    klog(LOG_DEBUG, "\tABI: %x\n", hdr->abi);
    klog(LOG_DEBUG, "\tType: %x\n", hdr->elf32.type);
    klog(LOG_DEBUG, "\tMachine: %x\n", hdr->elf32.mach);
    klog(LOG_DEBUG, "\tVersion: %x\n", hdr->elf32.elfv);
    klog(LOG_DEBUG, "\tEntry: %x\n", hdr->elf32.entry);
    klog(LOG_DEBUG, "\tProgram header table: %x\n", hdr->elf32.p_tbl);
    klog(LOG_DEBUG, "\tProgram header table entry size: %x\n", hdr->elf32.p_entry_sz);
    klog(LOG_DEBUG, "\tProgram header table entry count: %x\n", hdr->elf32.p_tbl_sz);
}

void* elf32_load(void *elf, struct mm_info *mm)
{
    struct elf_header *hdr = (struct elf_header*)elf;

    if (hdr->sig != 0x464c457f) {
        klog(LOG_ERROR, "Invalid ELF signature\n");
        return NULL;
    }

    if (hdr->elf32.mach != X86) {
        klog(LOG_ERROR, "Invalid machine type\n");
        return NULL;
    }
    struct elf32_pheader *phdr = (struct elf32_pheader*)((u32)elf + hdr->elf32.p_tbl);

#ifdef DEBUG_ELF
    elf_print(hdr);

    klog(LOG_DEBUG, "Program header table:\n");
    for (int i = 0; i < hdr->elf32.p_tbl_sz; i++) {
        klog(LOG_DEBUG, "Type: %x\n", phdr[i].type);
        klog(LOG_DEBUG, "Offset: %x\n", phdr[i].p_offset);
        klog(LOG_DEBUG, "Virt addr: %x\n", phdr[i].p_vaddr);
        klog(LOG_DEBUG, "File size: %x\n", phdr[i].p_filesz);
        klog(LOG_DEBUG, "Mem size: %x\n", phdr[i].p_memsz);
        klog(LOG_DEBUG, "Flags: %x\n", phdr[i].flags);
        klog(LOG_DEBUG, "Align: %x\n", phdr[i].align);
    }
#endif
    for (int i = 0; i < hdr->elf32.p_tbl_sz; i++) {
        if (phdr[i].type != LOAD_SEG)
            continue;
        if (phdr[i].align > PAGE_BYTES)
            kerror("Alignment greater than page size\n");
        int num_pages = phdr[i].p_memsz / PAGE_BYTES + 1;
        int flags = PG_USER;
        struct vm_desc *desc = kzmalloc(sizeof *desc);

        void *phys = alloc_frames(num_pages);
	    void *vaddr = (void*)(phdr[i].p_vaddr & 0xFFFFF000);

        map_pages(phys, vaddr, PG_USER | PG_WRITE, num_pages);

	    memcpy((void*)phdr[i].p_vaddr, (u8*)elf + phdr[i].p_offset, phdr[i].p_filesz);
        if (phdr[i].p_filesz < phdr[i].p_memsz)
            memset((void*)(phdr[i].p_vaddr + phdr[i].p_filesz), 0,
                    phdr[i].p_memsz - phdr[i].p_filesz);

        if (phdr[i].flags & WRIT) {
            flags |= PG_WRITE;
            desc->vm_prot |= PROT_WRITE;
        }
        if (phdr[i].flags & EXEC) {
            mm->start_code = (u32)phdr[i].p_vaddr;
            mm->end_code = (u32)phdr[i].p_vaddr + phdr[i].p_filesz;
            desc->vm_prot |= PROT_EXEC;
        }
        unmap_pages(vaddr, num_pages);
        map_pages(phys, vaddr, flags, num_pages);

        desc->mm = mm;
        desc->start = (uintptr_t)vaddr;
        desc->end = (uintptr_t)vaddr + num_pages * PAGE_BYTES;
        desc->vm_prot |= PROT_READ;
        desc->vm_flags = MAP_PRIVATE;

        if (desc->vm_prot & PROT_WRITE && desc->vm_prot & PROT_EXEC)
            klog(LOG_WARN, "Segment is both writable and executable\n");

        vma_list_insert(desc, &mm->mmap);
        // vma_rb_insert(desc, &mm->mmap_rb);
    }

    return (void*)hdr->elf32.entry;
}
