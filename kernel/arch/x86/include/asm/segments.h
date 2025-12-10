#ifndef __ASM_SEGMENTS_H
#define __ASM_SEGMENTS_H

#include <lilac/const.h>

#define GDT_ENTRY(flags, base, limit)			    \
    ((((base)  & _AC(0xff000000,ULL)) << (56-24)) |	\
     (((flags) & _AC(0x0000f0ff,ULL)) << 40) |	    \
     (((limit) & _AC(0x000f0000,ULL)) << (48-16)) |	\
     (((base)  & _AC(0x00ffffff,ULL)) << 16) |	    \
     (((limit) & _AC(0x0000ffff,ULL))))

#ifdef __x86_64__ /* 64-bit: */
#define GDT_ENTRY_KERN_CS32 1
#define GDT_ENTRY_KERN_CS   2
#define GDT_ENTRY_KERN_DS   3
#define GDT_ENTRY_USER_CS32 4
#define GDT_ENTRY_USER_DS   5
#define GDT_ENTRY_USER_CS   6
#define GDT_ENTRY_TSS(cpu_num) (7 + (cpu_num))

#define __KERNEL_CS32   (GDT_ENTRY_KERN_CS32*8)
#define __KERNEL_CS     (GDT_ENTRY_KERN_CS*8)
#define __KERNEL_DS     (GDT_ENTRY_KERN_DS*8)
#define __USER_CS32     (GDT_ENTRY_USER_CS32*8+3)
#define __USER_CS       (GDT_ENTRY_USER_CS*8+3)
#define __USER_DS       (GDT_ENTRY_USER_DS*8+3)
#define __TSS(cpu_num)  (GDT_ENTRY_TSS(cpu_num)*8)
#else /* 32-bit: */
#define GDT_ENTRY_KERN_CS 1
#define GDT_ENTRY_KERN_DS 2
#define GDT_ENTRY_USER_CS 3
#define GDT_ENTRY_USER_DS 4
#define GDT_ENTRY_PERCPU  5
#define GDT_ENTRY_TSS(cpu_num) (6 + (cpu_num))

#define __KERNEL_CS (GDT_ENTRY_KERN_CS*8)
#define __KERNEL_DS (GDT_ENTRY_KERN_DS*8)
#define __USER_CS   (GDT_ENTRY_USER_CS*8+3)
#define __USER_DS   (GDT_ENTRY_USER_DS*8+3)
#define __KERNEL_GS (GDT_ENTRY_PERCPU*8)
#define __TSS(cpu_num)  (GDT_ENTRY_TSS(cpu_num)*8)
#endif /* __x86_64__ */

#endif /* __ASM_SEGMENTS_H */
