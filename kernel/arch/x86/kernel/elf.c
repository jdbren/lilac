// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/elf.h>

#include <lilac/lilac.h>
#include <lilac/libc.h>
#include <lilac/process.h>
#include <mm/mm.h>
#include <mm/kmm.h>
#include <mm/page.h>

// #define DEBUG_ELF 1

void elf32_print(struct elf_header *hdr)
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

void elf64_print(struct elf_header *hdr)
{
    klog(LOG_DEBUG, "ELF header:\n");
    klog(LOG_DEBUG, "\tELF sig: %x\n", hdr->sig);
    klog(LOG_DEBUG, "\tClass: %x\n", hdr->class);
    klog(LOG_DEBUG, "\tEndian: %x\n", hdr->endian);
    klog(LOG_DEBUG, "\tHeader version: %x\n", hdr->headv);
    klog(LOG_DEBUG, "\tABI: %x\n", hdr->abi);
    klog(LOG_DEBUG, "\tType: %x\n", hdr->elf64.type);
    klog(LOG_DEBUG, "\tMachine: %x\n", hdr->elf64.mach);
    klog(LOG_DEBUG, "\tVersion: %x\n", hdr->elf64.elfv);
    klog(LOG_DEBUG, "\tEntry: %x\n", hdr->elf64.entry);
    klog(LOG_DEBUG, "\tProgram header table: %x\n", hdr->elf64.p_tbl);
    klog(LOG_DEBUG, "\tProgram header table entry size: %x\n", hdr->elf64.p_entry_sz);
    klog(LOG_DEBUG, "\tProgram header table entry count: %x\n", hdr->elf64.p_tbl_sz);
}

static void* elf32_load(void *elf, struct mm_info *mm)
{
    struct elf_header *hdr = (struct elf_header*)elf;

    if (hdr->elf32.mach != X86) {
        klog(LOG_ERROR, "Invalid machine type\n");
        return NULL;
    }
    struct elf32_pheader *phdr = (struct elf32_pheader*)((u8*)elf + hdr->elf32.p_tbl);

#ifdef DEBUG_ELF
    elf32_print(hdr);

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
        if (phdr[i].align > PAGE_SIZE)
            kerror("Alignment greater than page size\n");
        int num_pages = PAGE_ROUND_UP(phdr[i].p_memsz) / PAGE_SIZE;
        int flags = MEM_USER;
        struct vm_desc *desc = kzmalloc(sizeof *desc);
        if (!desc) {
            klog(LOG_ERROR, "Out of memory loading ELF\n");
            return NULL;
        }

        void *phys = alloc_frames(num_pages);
	    void *vaddr = (void*)(uintptr_t)(phdr[i].p_vaddr & 0xFFFFF000);

        map_pages(phys, vaddr, MEM_USER | MEM_WRITE, num_pages);

	    memcpy((void*)(uintptr_t)phdr[i].p_vaddr, (u8*)elf + phdr[i].p_offset, phdr[i].p_filesz);
        if (phdr[i].p_filesz < phdr[i].p_memsz)
            memset((void*)(uintptr_t)(phdr[i].p_vaddr + phdr[i].p_filesz), 0,
                    phdr[i].p_memsz - phdr[i].p_filesz);

        if (phdr[i].flags & WRIT) {
            flags |= MEM_WRITE;
            desc->vm_prot |= PROT_WRITE;
        }
        if (phdr[i].flags & EXEC) {
            mm->start_code = phdr[i].p_vaddr;
            mm->end_code = phdr[i].p_vaddr + phdr[i].p_filesz;
            desc->vm_prot |= PROT_EXEC;
        }
        unmap_pages(vaddr, num_pages);
        map_pages(phys, vaddr, flags, num_pages);

        desc->mm = mm;
        desc->start = (uintptr_t)vaddr;
        desc->end = (uintptr_t)vaddr + num_pages * PAGE_SIZE;
        desc->vm_prot |= PROT_READ;
        desc->vm_flags = MAP_PRIVATE;

        if (desc->vm_prot & PROT_WRITE && desc->vm_prot & PROT_EXEC)
            klog(LOG_WARN, "Segment is both writable and executable\n");

        vma_list_insert(desc, &mm->mmap);
        // vma_rb_insert(desc, &mm->mmap_rb);
    }

    return (void*)(uintptr_t)hdr->elf32.entry;
}

#ifdef __x86_64__
static void * elf64_load(void *elf, struct mm_info *mm)
{
    struct elf_header *hdr = (struct elf_header*)elf;

    if (hdr->elf64.mach != X86_64) {
        klog(LOG_ERROR, "Invalid machine type\n");
        return NULL;
    }

    struct elf64_pheader *phdr = (struct elf64_pheader*)((u8*)elf + hdr->elf64.p_tbl);
#ifdef DEBUG_ELF
    elf64_print(hdr);

    klog(LOG_DEBUG, "Program header table:\n");
    for (int i = 0; i < hdr->elf64.p_tbl_sz; i++) {
        klog(LOG_DEBUG, "Type: %x\n", phdr[i].type);
        klog(LOG_DEBUG, "Offset: %x\n", phdr[i].p_offset);
        klog(LOG_DEBUG, "Virt addr: %x\n", phdr[i].p_vaddr);
        klog(LOG_DEBUG, "File size: %x\n", phdr[i].p_filesz);
        klog(LOG_DEBUG, "Mem size: %x\n", phdr[i].p_memsz);
        klog(LOG_DEBUG, "Flags: %x\n", phdr[i].flags);
        klog(LOG_DEBUG, "Align: %x\n", phdr[i].align);
    }
#endif

    for (int i = 0; i < hdr->elf64.p_tbl_sz; i++) {
        if (phdr[i].type != LOAD_SEG)
            continue;
        if (phdr[i].align > PAGE_SIZE)
            kerror("Alignment greater than page size\n");
        int num_pages = PAGE_ROUND_UP(phdr[i].p_memsz) / PAGE_SIZE;
        int flags = MEM_USER;
        struct vm_desc *desc = kzmalloc(sizeof *desc);
        if (!desc) {
            klog(LOG_ERROR, "Out of memory loading ELF\n");
            return NULL;
        }

        void *phys = alloc_frames(num_pages);
        void *vaddr = (void*)(phdr[i].p_vaddr & 0xFFFFF000);

        map_pages(phys, vaddr, MEM_USER | MEM_WRITE, num_pages);

        memcpy((void*)phdr[i].p_vaddr, (u8*)elf + phdr[i].p_offset, phdr[i].p_filesz);
        if (phdr[i].p_filesz < phdr[i].p_memsz)
            memset((void*)(phdr[i].p_vaddr + phdr[i].p_filesz), 0,
                    phdr[i].p_memsz - phdr[i].p_filesz);

        if (phdr[i].flags & READ) {
            desc->vm_prot |= PROT_READ;
        }
        if (phdr[i].flags & WRIT) {
            flags |= MEM_WRITE;
            desc->vm_prot |= PROT_WRITE;
        }
        if (phdr[i].flags & EXEC) {
            mm->start_code = phdr[i].p_vaddr;
            mm->end_code = phdr[i].p_vaddr + phdr[i].p_filesz;
            desc->vm_prot |= PROT_EXEC;
        }
        unmap_pages(vaddr, num_pages);
        map_pages(phys, vaddr, flags, num_pages);

        desc->mm = mm;
        desc->start = (uintptr_t)vaddr;
        desc->end = (uintptr_t)vaddr + num_pages * PAGE_SIZE;
        desc->vm_flags = MAP_PRIVATE;

        if (desc->vm_prot & PROT_WRITE && desc->vm_prot & PROT_EXEC)
            klog(LOG_WARN, "Segment is both writable and executable\n");

        vma_list_insert(desc, &mm->mmap);
    }

    return (void*)hdr->elf64.entry;
}
#else
static void * elf64_load(void *elf, struct mm_info *mm) { return NULL; }
#endif

void * elf_load(struct elf_header *elf, struct mm_info *mm)
{
    if (elf->sig != 0x464c457f) {
        klog(LOG_ERROR, "Invalid ELF signature\n");
        return NULL;
    }

    if (elf->class == 1) {
        return elf32_load(elf, mm);
    } else if (elf->class == 2 && sizeof(void*) == 8) {
        return elf64_load(elf, mm);
    } else {
        klog(LOG_ERROR, "Invalid ELF class\n");
        return NULL;
    }
}
