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

static int elf32_load(struct elf_header *hdr, struct mm_info *mm, struct file *elff,
                      struct exec_info *info)
{
    if (hdr->elf32.mach != X86) {
        klog(LOG_ERROR, "Invalid machine type\n");
        return -EINVAL;
    }

    int n = hdr->elf32.p_tbl_num_ents;
    struct elf32_pheader *phdr = kmalloc(hdr->elf32.p_entry_sz * n);
    if (!phdr) {
        klog(LOG_ERROR, "Failed to allocate memory for program header table\n");
        return -ENOMEM;
    }
    if (vfs_read_at(elff, (void*)phdr,
            hdr->elf32.p_entry_sz * n,
            hdr->elf32.p_tbl_off) <= 0) {
        kfree(phdr);
        klog(LOG_ERROR, "Failed to read program header table\n");
        return -EIO;
    }

#ifdef DEBUG_ELF
    elf32_print(hdr);
    klog(LOG_DEBUG, "Program header table:\n");
    for (int i = 0; i < n; i++) {
        klog(LOG_DEBUG, "Type: %x\n", phdr[i].type);
        klog(LOG_DEBUG, "Offset: %x\n", phdr[i].p_offset);
        klog(LOG_DEBUG, "Virt addr: %x\n", phdr[i].p_vaddr);
        klog(LOG_DEBUG, "File size: %x\n", phdr[i].p_filesz);
        klog(LOG_DEBUG, "Mem size: %x\n", phdr[i].p_memsz);
        klog(LOG_DEBUG, "Flags: %x\n", phdr[i].flags);
        klog(LOG_DEBUG, "Align: %x\n", phdr[i].align);
    }
#endif

    for (int i = 0; i < n; i++) {
        if (phdr[i].type != LOAD_SEG)
            continue;
        if (phdr[i].align > PAGE_SIZE)
            kerror("Alignment greater than page size\n");

        struct vm_desc *desc = kzmalloc(sizeof *desc);
        if (!desc) {
            klog(LOG_ERROR, "Out of memory loading ELF\n");
            kfree(phdr);
            return -ENOMEM;
        }

        uintptr_t seg_vaddr = phdr[i].p_vaddr;  // exact segment VA
        uintptr_t vma_start = PAGE_ROUND_DOWN(seg_vaddr);
        uintptr_t vma_end = PAGE_ROUND_UP(seg_vaddr + phdr[i].p_memsz);

        if (phdr[i].flags & READ)
            desc->vm_flags |= VM_READ;
        if (phdr[i].flags & WRIT)
            desc->vm_flags |= VM_WRITE;
        if (phdr[i].flags & EXEC)
            desc->vm_flags |= VM_EXEC;

        desc->mm = mm;
        desc->start = vma_start;
        desc->end = vma_end;
        desc->vm_file = elff;
        desc->vm_pgoff = PAGE_ROUND_DOWN(phdr[i].p_offset) / PAGE_SIZE;
        desc->seg_vaddr = seg_vaddr;
        desc->seg_offset = phdr[i].p_offset;
        desc->vm_fsize = phdr[i].p_filesz; // file-backed size

        if ((desc->vm_flags & VM_WRITE) && (desc->vm_flags & VM_EXEC))
            klog(LOG_WARN, "Segment is both writable and executable\n");

        vma_list_insert(desc, &mm->mmap);
    }

    kfree(phdr);

    info->entry = (void*)(uintptr_t)hdr->elf32.entry;
    info->app_entry = info->entry;
    info->at_phdr = NULL;
    info->phnum = hdr->elf32.p_tbl_num_ents;
    info->phentsize = hdr->elf32.p_entry_sz;
    info->interp_base = 0;
    return 0;
}

#ifdef __x86_64__
#define INTERP_BASE 0x7f0000000000ULL

/*
 * Open and map the ELF interpreter (an ET_DYN shared object) at INTERP_BASE.
 * Returns the interpreter entry point (INTERP_BASE + e_entry), or 0 on error.
 */
static uintptr_t load_interp(const char *path, struct mm_info *mm)
{
    struct file *f = vfs_open(path, 0, 0);
    if (IS_ERR_OR_NULL(f)) {
        klog(LOG_ERROR, "load_interp: cannot open %s\n", path);
        return 0;
    }

    struct elf_header hdr;
    if (vfs_read_at(f, &hdr, sizeof hdr, 0) != sizeof hdr) {
        klog(LOG_ERROR, "load_interp: short read on %s\n", path);
        vfs_close(f);
        return 0;
    }

    if (hdr.sig != ELF_MAGIC || hdr.class != 2 || hdr.elf64.mach != X86_64) {
        klog(LOG_ERROR, "load_interp: bad ELF in %s\n", path);
        vfs_close(f);
        return 0;
    }

    int n = hdr.elf64.p_tbl_num_ents;
    struct elf64_pheader *phdr = kmalloc(hdr.elf64.p_entry_sz * n);
    if (!phdr) {
        vfs_close(f);
        return 0;
    }

    if (vfs_read_at(f, phdr, hdr.elf64.p_entry_sz * n, hdr.elf64.p_tbl_off) <= 0) {
        klog(LOG_ERROR, "load_interp: failed to read phdrs from %s\n", path);
        kfree(phdr);
        vfs_close(f);
        return 0;
    }

    for (int i = 0; i < n; i++) {
        if (phdr[i].type != LOAD_SEG || phdr[i].p_memsz == 0)
            continue;

        struct vm_desc *desc = kzmalloc(sizeof *desc);
        if (!desc) {
            kfree(phdr);
            vfs_close(f);
            return 0;
        }

        // Interpreter is ET_DYN: p_vaddr values are relative to INTERP_BASE.
        uintptr_t seg_vaddr = INTERP_BASE + phdr[i].p_vaddr;
        uintptr_t vma_start = PAGE_ROUND_DOWN(seg_vaddr);
        uintptr_t vma_end = PAGE_ROUND_UP(seg_vaddr + phdr[i].p_memsz);

        if (phdr[i].flags & READ)
            desc->vm_flags |= VM_READ;
        if (phdr[i].flags & WRIT)
            desc->vm_flags |= VM_WRITE;
        if (phdr[i].flags & EXEC)
            desc->vm_flags |= VM_EXEC;

        desc->mm = mm;
        desc->start = vma_start;
        desc->end = vma_end;
        desc->vm_file = f;
        desc->vm_pgoff = PAGE_ROUND_DOWN(phdr[i].p_offset) / PAGE_SIZE;
        desc->seg_vaddr = seg_vaddr;
        desc->seg_offset = phdr[i].p_offset;
        desc->vm_fsize = phdr[i].p_filesz;

        vma_list_insert(desc, &mm->mmap);
    }

    kfree(phdr);

    uintptr_t entry = INTERP_BASE + hdr.elf64.entry;
    klog(LOG_DEBUG, "load_interp: %s loaded at base %lx, entry %lx\n",
        path, (unsigned long)INTERP_BASE, (unsigned long)entry);
    return entry;
}

static int elf64_load(struct elf_header *hdr, struct mm_info *mm, struct file *elff,
                      struct exec_info *info)
{
    if (hdr->elf64.mach != X86_64) {
        klog(LOG_ERROR, "Invalid machine type\n");
        return -EINVAL;
    }

    int n = hdr->elf64.p_tbl_num_ents;
    struct elf64_pheader *phdr = kmalloc(hdr->elf64.p_entry_sz * n);
    if (!phdr) {
        klog(LOG_ERROR, "Failed to allocate memory for program header table\n");
        return -ENOMEM;
    }

    if (vfs_read_at(elff, (void*)phdr,
            hdr->elf64.p_entry_sz * n,
            hdr->elf64.p_tbl_off) <= 0) {
        kfree(phdr);
        klog(LOG_ERROR, "Failed to read program header table\n");
        return -EIO;
    }

#ifdef DEBUG_ELF
    elf64_print(hdr);
    klog(LOG_DEBUG, "Program header table:\n");
    for (int i = 0; i < n; i++) {
        klog(LOG_DEBUG, "Type: %x\n", phdr[i].type);
        klog(LOG_DEBUG, "Offset: %x\n", phdr[i].p_offset);
        klog(LOG_DEBUG, "Virt addr: %x\n", phdr[i].p_vaddr);
        klog(LOG_DEBUG, "File size: %x\n", phdr[i].p_filesz);
        klog(LOG_DEBUG, "Mem size: %x\n", phdr[i].p_memsz);
        klog(LOG_DEBUG, "Flags: %x\n", phdr[i].flags);
        klog(LOG_DEBUG, "Align: %x\n", phdr[i].align);
    }
#endif

    /*
     * First pass: PT_PHDR (for AT_PHDR), PT_INTERP path, and
     * the first PT_LOAD (for AT_PHDR fallback computation)
     */
    char *interp_path = NULL;
    void *at_phdr = NULL;
    bool have_phdr_seg = false;
    u64 first_load_vaddr = 0, first_load_offset = (u64)-1;

    for (int i = 0; i < n; i++) {
        switch (phdr[i].type) {
        case PHDR_SEG:
            at_phdr = (void *)phdr[i].p_vaddr;
            have_phdr_seg = true;
            break;
        case INTERP_SEG:
            if (phdr[i].p_filesz == 0 ||
                phdr[i].p_filesz >= PATH_MAX) {
                klog(LOG_ERROR, "PT_INTERP: bad path length %llu\n",
                     (unsigned long long)phdr[i].p_filesz);
                kfree(phdr);
                return -EINVAL;
            }
            interp_path = kmalloc(PATH_MAX);
            if (!interp_path) {
                klog(LOG_ERROR, "Failed to allocate memory for PT_INTERP path\n");
                kfree(phdr);
                return -ENOMEM;
            }
            if (vfs_read_at(elff, interp_path,
                    phdr[i].p_filesz, phdr[i].p_offset) <= 0) {
                kfree(phdr);
                kfree(interp_path);
                klog(LOG_ERROR, "Failed to read PT_INTERP path\n");
                return -EIO;
            }
            interp_path[phdr[i].p_filesz] = '\0';
            break;
        case LOAD_SEG:
            if (phdr[i].p_offset < first_load_offset) {
                first_load_offset = phdr[i].p_offset;
                first_load_vaddr  = phdr[i].p_vaddr;
            }
            break;
        default:
            break;
        }
    }

    /*
     * AT_PHDR fallback: if no PT_PHDR segment, compute from first PT_LOAD.
     * AT_PHDR = p_vaddr_of_first_load - p_offset_of_first_load + e_phoff.
     * For ET_EXEC this equals the virtual address of the ELF header + e_phoff.
     */
    if (!have_phdr_seg && first_load_offset != (u64)-1)
        at_phdr = (void *)(uintptr_t)(first_load_vaddr - first_load_offset
                                      + hdr->elf64.p_tbl_off);

    // Second pass: create VMAs for PT_LOAD segments
    for (int i = 0; i < n; i++) {
        if (phdr[i].type != LOAD_SEG)
            continue;
        if (phdr[i].align > PAGE_SIZE)
            kerror("Alignment greater than page size\n");

        struct vm_desc *desc = kzmalloc(sizeof *desc);
        if (!desc) {
            klog(LOG_ERROR, "Out of memory loading ELF\n");
            if (interp_path)
                kfree(interp_path);
            kfree(phdr);
            return -ENOMEM;
        }

        uintptr_t seg_vaddr = phdr[i].p_vaddr;  // exact segment VA
        uintptr_t vma_start = PAGE_ROUND_DOWN(seg_vaddr);
        uintptr_t vma_end = PAGE_ROUND_UP(seg_vaddr + phdr[i].p_memsz);

        if (phdr[i].flags & READ)
            desc->vm_flags |= VM_READ;
        if (phdr[i].flags & WRIT)
            desc->vm_flags |= VM_WRITE;
        if (phdr[i].flags & EXEC)
            desc->vm_flags |= VM_EXEC;

        desc->mm = mm;
        desc->start = vma_start;
        desc->end = vma_end;
        desc->vm_file = elff;
        desc->vm_pgoff = PAGE_ROUND_DOWN(phdr[i].p_offset) / PAGE_SIZE;
        desc->seg_vaddr = seg_vaddr;
        desc->seg_offset = phdr[i].p_offset;
        desc->vm_fsize = phdr[i].p_filesz; // file-backed size

        if ((desc->vm_flags & VM_WRITE) && (desc->vm_flags & VM_EXEC))
            klog(LOG_WARN, "Segment is both writable and executable\n");

        vma_list_insert(desc, &mm->mmap);
    }

    kfree(phdr);

    info->app_entry = (void *)hdr->elf64.entry;
    info->at_phdr = at_phdr;
    info->phnum = (u16)hdr->elf64.p_tbl_num_ents;
    info->phentsize = (u16)hdr->elf64.p_entry_sz;

    if (interp_path) {
        klog(LOG_DEBUG, "elf64_load: interpreter = %s\n", interp_path);
        uintptr_t interp_entry = load_interp(interp_path, mm);
        if (!interp_entry) {
            klog(LOG_ERROR, "Failed to load interpreter: %s\n", interp_path);
            kfree(interp_path);
            return -ENOENT;
        }
        info->interp_base = INTERP_BASE;
        info->entry       = (void *)interp_entry;
    } else {
        info->interp_base = 0;
        info->entry = info->app_entry;
    }

    kfree(interp_path);
    return 0;
}
#else
static int elf64_load(struct elf_header *hdr, struct mm_info *mm, struct file *elff,
                      struct exec_info *info)
{
    return -ENOEXEC;
}
#endif

int elf_load(struct file *f, struct mm_info *mm, struct exec_info *info)
{
    struct elf_header elf;
    if (vfs_read_at(f, (void*)&elf, sizeof elf, 0) != sizeof elf)
        return -EIO;

    if (elf.sig != ELF_MAGIC) {
        klog(LOG_ERROR, "Invalid ELF signature\n");
        return -ENOEXEC;
    }

    if (elf.class == 1) {
        return elf32_load(&elf, mm, f, info);
    } else if (elf.class == 2 && sizeof(void*) == 8) {
        return elf64_load(&elf, mm, f, info);
    } else {
        klog(LOG_ERROR, "Invalid ELF class\n");
        return -ENOEXEC;
    }
}
