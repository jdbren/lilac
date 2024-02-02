.intel_syntax noprefix
.global gdt_set
.global reload_segments
.global flush_tss

gdt_set:
   mov  eax, [esp + 4] # Load address of GDT
   lgdt [eax] # Load GDT
   ret

reload_segments:
   # Reload CS register containing code selector:
   jmp   0x08:.reload_CS
.reload_CS:
   # Reload data segment registers:
   mov   ax, 0x10
   mov   ds, ax
   mov   es, ax
   mov   fs, ax
   mov   gs, ax
   mov   ss, ax
   ret

flush_tss:
   mov ax, (5 * 8) | 0  # RPL 0
	ltr ax
	ret
