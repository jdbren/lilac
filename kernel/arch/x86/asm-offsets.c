#include <lilac/process.h>
#include <lilac/percpu.h>

#define DEFINE(sym, val) \
    asm volatile("\n#define " #sym " %0" : : "i" (val))

void output_offsets(void)
{
    DEFINE(CPU_LOCAL_USTACK, offsetof(struct cpu_local, user_stack));
    DEFINE(CPU_LOCAL_TSS, offsetof(struct cpu_local, priv));
    DEFINE(CPU_LOCAL_ID, offsetof(struct cpu_local, id));
    DEFINE(CPU_LOCAL_SCRATCH, offsetof(struct cpu_local, scratch));

    DEFINE(TASK_PGD_OFFSET, offsetof(struct task, pgd));
    DEFINE(TASK_PC_OFFSET, offsetof(struct task, pc));
    DEFINE(TASK_KSTACK_OFFSET, offsetof(struct task, kstack));
}
