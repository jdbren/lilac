#include <kernel/elf.h>

void* elf32_load(void *elf)
{
    struct elf_header *hdr = (struct elf_header*)elf;
    struct elf32_pheader *phdr = (struct elf32_pheader*)((u32)elf + hdr->elf32.p_tbl);
    
    printf("ELF sig: %x\n", hdr->sig);
    printf("Class: %x\n", hdr->class);
    printf("Endian: %x\n", hdr->endian);
    printf("Header version: %x\n", hdr->headv);
    printf("ABI: %x\n", hdr->abi);
    printf("Type: %x\n", hdr->elf32.type);
    printf("Machine: %x\n", hdr->elf32.mach);
    printf("Version: %x\n", hdr->elf32.elfv);
    printf("Entry: %x\n", hdr->elf32.entry);

    return (void*)hdr->elf32.entry;   
}
