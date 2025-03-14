#include <lilac/types.h>
#include <lilac/process.h>

#include "gdt.h"

void jump_new_proc(struct task *next)
{
    set_tss_esp0((u64)next->kstack);
    asm volatile (
        "mov %[next_pg], %%rax\n\t"
        "mov %%rax, %%cr3\n\t"
        "mov %[next_sp], %%rsp\n\t"
        "push %[next_ip]\n\t"
        "retq\n"
        :
        :   [next_sp] "m" (next->kstack),
            [next_ip] "m" (next->pc),
            [next_pg] "m" (next->pgd)
        :   "memory"
    );
}
