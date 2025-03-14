#ifndef _X86_REGS_H
#define _X86_REGS_H

#ifndef __ASSEMBLY__

#include <lilac/types.h>

#ifdef ARCH_x86_64
struct regs_state {
    unsigned long bx;
    unsigned long cx;
    unsigned long dx;
    unsigned long si;
    unsigned long di;
    unsigned long r15;
    unsigned long r14;
    unsigned long r13;
    unsigned long r12;
    unsigned long r11;
    unsigned long r10;
    unsigned long r9;
    unsigned long r8;
    unsigned long bp;
    unsigned long ax;
    unsigned long ip;
    unsigned long cs;
    unsigned long flags;
    unsigned long sp;
    unsigned long ss;
};
#else /* 32-bit: */
struct regs_state {
    unsigned long bx;
    unsigned long cx;
    unsigned long dx;
    unsigned long si;
    unsigned long di;
    unsigned long bp;
    unsigned long ax;
    unsigned long ds;
    unsigned long es;
    unsigned long fs;
    unsigned long gs;
    unsigned long ip;
    unsigned long cs;
    unsigned long flags;
    unsigned long sp;
    unsigned long ss;
};
#endif /* ARCH_x86_64 */

#endif /* !__ASSEMBLY__ */

#ifdef ARCH_x86_64
#define RBX 0
#define RCX 8
#define RDX 16
#define RSI 24
#define RDI 32
#define R15 40
#define R14 48
#define R13 56
#define R12 64
#define R11 72
#define R10 80
#define R9  88
#define R8  96
#define RBP 104
#define RAX 112
#define RIP 120
#define CS  128
#define RFLAGS 136
#define RSP 144
#define SS  152
#else /* 32-bit: */
#define EBX 0
#define ECX 4
#define EDX 8
#define ESI 12
#define EDI 16
#define EBP 20
#define EAX 24
#define DS 28
#define ES 32
#define FS 36
#define GS 40
#define EIP 44
#define CS 48
#define EFLAGS 52
#define ESP 56
#define SS 60
#endif /* ARCH_x86_64 */

#endif /* _X86_REGS_H */
