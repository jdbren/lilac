#include "multiboot.h"

#define CHECK_FLAG(flags,bit)   ((flags) & (1 << (bit)))

void print_multiboot_info(multiboot_info_t *mbi) {
  /* Print out the flags. */
  printf ("flags = %x\n", (unsigned) mbi->flags);

  /* Are mem_* valid? */
  if (CHECK_FLAG (mbi->flags, 0))
    printf ("mem_lower = %dKB, mem_upper = %dKB\n",
            (unsigned) mbi->mem_lower, (unsigned) mbi->mem_upper);

  /* Is boot_device valid? */
  if (CHECK_FLAG (mbi->flags, 1))
    printf ("boot_device = %x\n", (unsigned) mbi->boot_device);
  
  /* Is the command line passed? */
  if (CHECK_FLAG (mbi->flags, 2))
    printf ("cmdline = %s\n", (char *) mbi->cmdline);

  /* Are mods_* valid? */
  if (CHECK_FLAG (mbi->flags, 3))
    {
      multiboot_module_t *mod;
      int i;
      
      printf ("mods_count = %d, mods_addr = %x\n",
              (int) mbi->mods_count, (int) mbi->mods_addr);
      for (i = 0, mod = (multiboot_module_t *) mbi->mods_addr;
           i < mbi->mods_count;
           i++, mod++)
        printf (" mod_start = %x, mod_end = %x, cmdline = %s\n",
                (unsigned) mod->mod_start,
                (unsigned) mod->mod_end,
                (char *) mod->cmdline);
    }

  /* Bits 4 and 5 are mutually exclusive! */
  if (CHECK_FLAG (mbi->flags, 4) && CHECK_FLAG (mbi->flags, 5))
    {
      printf ("Both bits 4 and 5 are set.\n");
      return;
    }

  /* Is the symbol table of a.out valid? */
  if (CHECK_FLAG (mbi->flags, 4))
    {
      multiboot_aout_symbol_table_t *multiboot_aout_sym = &(mbi->u.aout_sym);
      
      printf ("multiboot_aout_symbol_table: tabsize = %x, "
              "strsize = %x, addr = %x\n",
              (unsigned) multiboot_aout_sym->tabsize,
              (unsigned) multiboot_aout_sym->strsize,
              (unsigned) multiboot_aout_sym->addr);
    }

  /* Is the section header table of ELF valid? */
  if (CHECK_FLAG (mbi->flags, 5))
    {
      multiboot_elf_section_header_table_t *multiboot_elf_sec = &(mbi->u.elf_sec);

      printf ("multiboot_elf_sec: num = %d, size = %x,"
              " addr = %x, shndx = %x\n",
              (unsigned) multiboot_elf_sec->num, (unsigned) multiboot_elf_sec->size,
              (unsigned) multiboot_elf_sec->addr, (unsigned) multiboot_elf_sec->shndx);
    }

  /* Are mmap_* valid? */
  if (CHECK_FLAG (mbi->flags, 6))
    {
      	int i;
		for(i = 0; i < mbi->mmap_length; 
			i += sizeof(multiboot_memory_map_t)) 
		{
			multiboot_memory_map_t* mmmt = 
				(multiboot_memory_map_t*) (mbi->mmap_addr + i);
	
			printf("Start Addr: %x | Length: %x | Size: %x | Type: %d\n",
				mmmt->addr_high + mmmt->addr_low, mmmt->len_high + mmmt->len_low, mmmt->size, mmmt->type);
	
			if(mmmt->type == MULTIBOOT_MEMORY_AVAILABLE) {
				/* 
				* Do something with this memory block!
				* BE WARNED that some of memory shown as availiable is actually 
				* actively being used by the kernel! You'll need to take that
				* into account before writing to memory!
				*/
			}
		}
	}
}