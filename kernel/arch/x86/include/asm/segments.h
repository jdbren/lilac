#ifndef __ASM_SEGMENTS_H
#define __ASM_SEGMENTS_H

#ifdef ARCH_x86_64
#define __NULL_SEGMENT  0
#define __KERNEL_CS32   0x08
#define __KERNEL_CS     0x10
#define __KERNEL_DS     0x18
#else /* 32-bit */
#define __KERNEL_CS 0x08
#define __KERNEL_DS 0x10
#define __USER_CS   0x1B
#define __USER_DS   0x23
#endif /* 32-bit */

#endif /* __ASM_SEGMENTS_H */
