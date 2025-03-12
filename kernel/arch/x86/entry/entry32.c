#include <lilac/types.h>
#include <lilac/log.h>
#include <lilac/process.h>
#include "gdt.h"

/*
void arch_context_switch(struct task *prev, struct task *next)
{
    klog(LOG_DEBUG, "Switching from %s to %s\n", prev->name, next->name);
	set_tss_esp0((u32)next->kstack);
    asm volatile (
        "pushf\n\t"
        "movl %%esp, %[prev_sp]\n\t"
        "movl %[next_pg], %%eax\n\t"
        "movl %%eax, %%cr3\n\t"
        "movl %[next_sp], %%esp\n\t"
        "movl $1f, %[prev_ip]\n\t"
        "pushl %[next_ip]\n\t"
        "ret\n"
        "1:\t"
        "popf\n\t"
        :   [prev_sp] "=m" (prev->kstack),
            [prev_ip] "=m" (prev->pc)
        :   [next_sp] "m" (next->kstack),
            [next_ip] "m" (next->pc),
            [next_pg] "m" (next->pgd)
        :   "memory", "eax", "esi", "ebx", "edi"
    );
}
*/

void jump_new_proc(struct task *next)
{
    set_tss_esp0((u32)next->kstack);
    asm volatile (
        "movl %[next_pg], %%eax\n\t"
        "movl %%eax, %%cr3\n\t"
        "movl %[next_sp], %%esp\n\t"
        "pushl %[next_ip]\n\t"
        "ret\n"
        :
        :   [next_sp] "m" (next->kstack),
            [next_ip] "m" (next->pc),
            [next_pg] "m" (next->pgd)
        :   "memory"
    );
}
