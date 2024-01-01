.intel_syntax noprefix
.global gdt_set
.global reload_segments

gdt_set:
   mov  eax, [esp + 4] # Load address of GDT
   lgdt [eax] # Load GDT
   ret

reload_segments:
   # Reload CS register containing code selector:
   jmp   0x08:.reload_CS # 0x08 is a stand-in for your code segment
.reload_CS:
   # Reload data segment registers:
   mov   ax, 0x10 # 0x10 is a stand-in for your data segment
   mov   ds, ax
   mov   es, ax
   mov   fs, ax
   mov   gs, ax
   mov   ss, ax
   ret
