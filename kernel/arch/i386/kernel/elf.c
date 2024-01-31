#include <kernel/elf.h>

void elf32_load(void *elf, void *dest)
{
    struct elf32_header *hdr = (struct elf32_header*)elf;
    struct elf32_pheader *phdr = (struct elf32_pheader*)((u32)elf + hdr->p_tbl);
    
    printf("ELF type: %x\n", hdr->type);
    
}
