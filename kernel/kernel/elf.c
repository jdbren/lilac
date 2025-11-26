// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/elf.h>

#include <lilac/lilac.h>
#include <lilac/libc.h>
#include <lilac/process.h>
#include <lilac/fs.h>
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
    klog(LOG_DEBUG, "\tHeader version: %x\n", hdr->header_ver);
    klog(LOG_DEBUG, "\tABI: %x\n", hdr->abi);
    klog(LOG_DEBUG, "\tType: %x\n", hdr->elf32.type);
    klog(LOG_DEBUG, "\tMachine: %x\n", hdr->elf32.mach);
    klog(LOG_DEBUG, "\tVersion: %x\n", hdr->elf32.elfv);
    klog(LOG_DEBUG, "\tEntry: %x\n", hdr->elf32.entry);
    klog(LOG_DEBUG, "\tProgram header table: %x\n", hdr->elf32.p_tbl_off);
    klog(LOG_DEBUG, "\tProgram header table entry size: %x\n", hdr->elf32.p_entry_sz);
    klog(LOG_DEBUG, "\tProgram header table entry count: %x\n", hdr->elf32.p_tbl_num_ents);
}

void elf64_print(struct elf_header *hdr)
{
    klog(LOG_DEBUG, "ELF header:\n");
    klog(LOG_DEBUG, "\tELF sig: %x\n", hdr->sig);
    klog(LOG_DEBUG, "\tClass: %x\n", hdr->class);
    klog(LOG_DEBUG, "\tEndian: %x\n", hdr->endian);
    klog(LOG_DEBUG, "\tHeader version: %x\n", hdr->header_ver);
    klog(LOG_DEBUG, "\tABI: %x\n", hdr->abi);
    klog(LOG_DEBUG, "\tType: %x\n", hdr->elf64.type);
    klog(LOG_DEBUG, "\tMachine: %x\n", hdr->elf64.mach);
    klog(LOG_DEBUG, "\tVersion: %x\n", hdr->elf64.elfv);
    klog(LOG_DEBUG, "\tEntry: %x\n", hdr->elf64.entry);
    klog(LOG_DEBUG, "\tProgram header table: %x\n", hdr->elf64.p_tbl_off);
    klog(LOG_DEBUG, "\tProgram header table entry size: %x\n", hdr->elf64.p_entry_sz);
    klog(LOG_DEBUG, "\tProgram header table entry count: %x\n", hdr->elf64.p_tbl_num_ents);
}

static void* elf32_load(struct elf_header *hdr, struct mm_info *mm, struct file *elff)
{
    if (hdr->elf32.mach != X86) {
        klog(LOG_ERROR, "Invalid machine type\n");
        return NULL;
    }

    struct elf32_pheader *phdr = kmalloc(hdr->elf32.p_entry_sz * hdr->elf32.p_tbl_num_ents);
    if (!phdr) {
        klog(LOG_ERROR, "Failed to allocate memory for program header table\n");
        return NULL;
    }
    if (vfs_read_at(elff, (void*)phdr,
            hdr->elf32.p_entry_sz * hdr->elf32.p_tbl_num_ents,
            hdr->elf32.p_tbl_off) <= 0) {
        kfree(phdr);
        klog(LOG_ERROR, "Failed to read program header table\n");
        return NULL;
    }

#ifdef DEBUG_ELF
    elf32_print(hdr);
    klog(LOG_DEBUG, "Program header table:\n");
    for (int i = 0; i < hdr->elf32.p_tbl_num_ents; i++) {
        klog(LOG_DEBUG, "Type: %x\n", phdr[i].type);
        klog(LOG_DEBUG, "Offset: %x\n", phdr[i].p_offset);
        klog(LOG_DEBUG, "Virt addr: %x\n", phdr[i].p_vaddr);
        klog(LOG_DEBUG, "File size: %x\n", phdr[i].p_filesz);
        klog(LOG_DEBUG, "Mem size: %x\n", phdr[i].p_memsz);
        klog(LOG_DEBUG, "Flags: %x\n", phdr[i].flags);
        klog(LOG_DEBUG, "Align: %x\n", phdr[i].align);
    }
#endif

    for (int i = 0; i < hdr->elf32.p_tbl_num_ents; i++) {
        if (phdr[i].type != LOAD_SEG)
            continue;
        if (phdr[i].align > PAGE_SIZE)
            kerror("Alignment greater than page size\n");

        struct vm_desc *desc = kzmalloc(sizeof *desc);
        if (!desc) {
            klog(LOG_ERROR, "Out of memory loading ELF\n");
            return NULL;
        }

        int num_pages = PAGE_UP_COUNT(phdr[i].p_memsz);
        uintptr_t vaddr = PAGE_ROUND_DOWN(phdr[i].p_vaddr);

        if (phdr[i].flags & READ)
            desc->vm_flags |= VM_READ;
        if (phdr[i].flags & WRIT)
            desc->vm_flags |= VM_WRITE;
        if (phdr[i].flags & EXEC) {
            mm->start_code = phdr[i].p_vaddr;
            mm->end_code = phdr[i].p_vaddr + phdr[i].p_filesz;
            desc->vm_flags |= VM_EXEC;
        }

        desc->mm = mm;
        desc->start = vaddr;
        desc->end = vaddr + num_pages * PAGE_SIZE;
        desc->vm_file = elff;
        desc->vm_pgoff = phdr[i].p_offset / PAGE_SIZE;
        desc->vm_fsize = phdr[i].p_filesz;

        if (desc->vm_flags & VM_WRITE && desc->vm_flags & VM_EXEC)

    kfree(phdr);

        vma_list_insert(desc, &mm->mmap);
    }

    return (void*)(uintptr_t)hdr->elf32.entry;
}

#ifdef __x86_64__
static void * elf64_load(struct elf_header *hdr, struct mm_info *mm, struct file *elff)
{
    if (hdr->elf64.mach != X86_64) {
    struct elf64_pheader *phdr = kmalloc(hdr->elf64.p_entry_sz * hdr->elf64.p_tbl_num_ents);
    if (!phdr) {
        klog(LOG_ERROR, "Failed to allocate memory for program header table\n");
        return NULL;
    }
        return NULL;
    }

    struct elf64_pheader *phdr = kmalloc(hdr->elf64.p_entry_sz * hdr->elf64.p_tbl_num_ents);
    if (vfs_read_at(elff, (void*)phdr,
            hdr->elf64.p_entry_sz * hdr->elf64.p_tbl_num_ents,
            hdr->elf64.p_tbl_off) <= 0) {
        kfree(phdr);
        klog(LOG_ERROR, "Failed to read program header table\n");
        return NULL;
    }
#ifdef DEBUG_ELF
    elf64_print(hdr);
    klog(LOG_DEBUG, "Program header table:\n");
    for (int i = 0; i < hdr->elf64.p_tbl_num_ents; i++) {
        klog(LOG_DEBUG, "Type: %x\n", phdr[i].type);
        klog(LOG_DEBUG, "Offset: %x\n", phdr[i].p_offset);
        klog(LOG_DEBUG, "Virt addr: %x\n", phdr[i].p_vaddr);
        klog(LOG_DEBUG, "File size: %x\n", phdr[i].p_filesz);
        klog(LOG_DEBUG, "Mem size: %x\n", phdr[i].p_memsz);
        klog(LOG_DEBUG, "Flags: %x\n", phdr[i].flags);
        klog(LOG_DEBUG, "Align: %x\n", phdr[i].align);
    }
#endif

    for (int i = 0; i < hdr->elf64.p_tbl_num_ents; i++) {
        if (phdr[i].type != LOAD_SEG)
            continue;
        if (phdr[i].align > PAGE_SIZE)
            kerror("Alignment greater than page size\n");

        struct vm_desc *desc = kzmalloc(sizeof *desc);
        if (!desc) {
            klog(LOG_ERROR, "Out of memory loading ELF\n");
            return NULL;
        }

        int num_pages = PAGE_UP_COUNT(phdr[i].p_memsz);
        uintptr_t vaddr = PAGE_ROUND_DOWN(phdr[i].p_vaddr);

        if (phdr[i].flags & READ)
            desc->vm_flags |= VM_READ;
        if (phdr[i].flags & WRIT)
            desc->vm_flags |= VM_WRITE;
        if (phdr[i].flags & EXEC) {
            mm->start_code = phdr[i].p_vaddr;
            mm->end_code = phdr[i].p_vaddr + phdr[i].p_filesz;
            desc->vm_flags |= VM_EXEC;
        }

        desc->mm = mm;
        desc->start = vaddr;
        desc->end = vaddr + num_pages * PAGE_SIZE;
        desc->vm_file = elff;
        desc->vm_pgoff = phdr[i].p_offset / PAGE_SIZE;
        desc->vm_fsize = phdr[i].p_filesz;

        if (desc->vm_flags & VM_WRITE && desc->vm_flags & VM_EXEC)
            klog(LOG_WARN, "Segment is both writable and executable\n");

        vma_list_insert(desc, &mm->mmap);
    }

    kfree(phdr);
    return (void*)hdr->elf64.entry;
}
#else
static void * elf64_load(void *elf, struct mm_info *mm) { return NULL; }
#endif

void * elf_load(struct file *f, struct mm_info *mm)
{
    struct elf_header elf;
    if (vfs_read_at(f, (void*)&elf, sizeof elf, 0) != sizeof elf)
        return NULL;

    if (elf.sig != 0x464c457f) {
        klog(LOG_ERROR, "Invalid ELF signature\n");
        return NULL;
    }

    if (elf.class == 1) {
        return elf32_load(&elf, mm, f);
    } else if (elf.class == 2 && sizeof(void*) == 8) {
        return elf64_load(&elf, mm, f);
    } else {
        klog(LOG_ERROR, "Invalid ELF class\n");
        return NULL;
    }
}
