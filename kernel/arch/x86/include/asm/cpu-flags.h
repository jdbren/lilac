#ifndef _ASM_FLAGS_H
#define _ASM_FLAGS_H

#define X86_FLAGS_CF	(1 << 0) /* Carry Flag */
#define X86_FLAGS_PF	(1 << 2) /* Parity Flag */
#define X86_FLAGS_AF	(1 << 4) /* Auxiliary carry Flag */
#define X86_FLAGS_ZF	(1 << 6) /* Zero Flag */
#define X86_FLAGS_SF	(1 << 7) /* Sign Flag */
#define X86_FLAGS_TF	(1 << 8) /* Trap Flag */
#define X86_FLAGS_IF	(1 << 9) /* Interrupt Flag */
#define X86_FLAGS_DF	(1 << 10) /* Direction Flag */
#define X86_FLAGS_OF	(1 << 11) /* Overflow Flag */
#define X86_FLAGS_IOPL	(3 << 12) /* I/O Privilege Level */
#define X86_FLAGS_NT	(1 << 14) /* Nested Task */
#define X86_FLAGS_RF	(1 << 16) /* Resume Flag */
#define X86_FLAGS_VM	(1 << 17) /* Virtual 8086 mode */
#define X86_FLAGS_AC	(1 << 18) /* Alignment Check */
#define X86_FLAGS_VIF	(1 << 19) /* Virtual Interrupt Flag */
#define X86_FLAGS_VIP	(1 << 20) /* Virtual Interrupt Pending */
#define X86_FLAGS_ID	(1 << 21) /* ID flag */

#define X86_CR0_PE	(1 << 0) /* Protected mode */
#define X86_CR0_MP	(1 << 1) /* Monitor co-processor */
#define X86_CR0_EM	(1 << 2) /* Emulation */
#define X86_CR0_TS	(1 << 3) /* Task switched */
#define X86_CR0_ET	(1 << 4) /* Extension type */
#define X86_CR0_NE	(1 << 5) /* Numeric error */
#define X86_CR0_WP	(1 << 16) /* Write protect */
#define X86_CR0_AM	(1 << 18) /* Alignment mask */
#define X86_CR0_NW	(1 << 29) /* Not write-through */
#define X86_CR0_CD	(1 << 30) /* Cache disable */
#define X86_CR0_PG	(1 << 31) /* Paging */

#define X86_CR4_VME	(1 << 0) /* Virtual-8086 mode extensions */
#define X86_CR4_PVI	(1 << 1) /* Protected-mode virtual interrupts */
#define X86_CR4_TSD	(1 << 2) /* Time stamp disable */
#define X86_CR4_DE	(1 << 3) /* Debugging extensions */
#define X86_CR4_PSE	(1 << 4) /* Page size extensions */
#define X86_CR4_PAE	(1 << 5) /* Physical address extensions */
#define X86_CR4_MCE	(1 << 6) /* Machine check enable */
#define X86_CR4_PGE	(1 << 7) /* Page global enable */
#define X86_CR4_PCE	(1 << 8) /* Performance monitoring counter enable */
#define X86_CR4_OSFXSR	(1 << 9) /* OS support for FXSAVE/FXRSTOR */
#define X86_CR4_OSXMMEXCPT	(1 << 10) /* OS support for unmasked SIMD floating-point exceptions */
#define X86_CR4_VMXE	(1 << 13) /* Virtual machine extensions enable */
#define X86_CR4_SMXE	(1 << 14) /* Safer mode extensions enable */
#define X86_CR4_PCIDE	(1 << 17) /* PCID enable */
#define X86_CR4_OSXSAVE	(1 << 18) /* XSAVE and processor extended states enable */
#define X86_CR4_SMEP	(1 << 20) /* Supervisor mode execution protection enable */
#define X86_CR4_SMAP	(1 << 21) /* Supervisor mode access protection enable */

#define EFER_SCE (1 << 0) /* System call extensions */
#define EFER_LME (1 << 8) /* Long mode enable */
#define EFER_LMA (1 << 10) /* Long mode active */
#define EFER_NXE (1 << 11) /* No-execute enable */
#define EFER_SVME (1 << 12) /* Secure virtual machine enable */
#define EFER_LMSLE (1 << 13) /* Long mode segment limit enable */
#define EFER_FFXSR (1 << 14) /* Fast FXSAVE/FXRSTOR */
#define EFER_TCE (1 << 15) /* Translation cache extension */

#endif /* _ASM_FLAGS_H */
