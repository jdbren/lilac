#include <lilac/sched.h>
#include <lilac/percpu.h>
#include <asm/cpu.h>
#include <asm/regs.h>

void save_fp_regs(struct task *p)
{
    if (!p->fp_regs) {
        p->fp_regs = kmalloc(512);
        if (!p->fp_regs)
            panic("Out of memory allocating FP state");
    }

    __builtin_ia32_fxsave64(p->fp_regs);
}

void restore_fp_regs(struct task *p)
{
    if (!p->fp_regs) {
        asm volatile ("fninit");
        return;
    }

    __builtin_ia32_fxrstor64(p->fp_regs);
}

void copy_fp_regs(struct task *dst, struct task *src)
{
    struct cpu_local *cpu = this_cpu_local();

    if (cpu->fpu_owner == src)
        save_fp_regs(src);

    if (!src->fp_regs)
        return;  // not used

    dst->fp_regs = kmalloc(512);
    if (!dst->fp_regs)
        panic("Out of memory allocating FP state");

    memcpy(dst->fp_regs, src->fp_regs, 512);
}

void fpu_switch(struct task *prev, struct task *next)
{
    struct cpu_local *cpu = this_cpu_local();

    if (cpu->fpu_owner == prev) {
        save_fp_regs(prev);
        cpu->fpu_owner = NULL;
    }

    write_cr0(read_cr0() | X86_CR0_TS);
}

void fpu_nm_exception(void)
{
    struct cpu_local *cpu = this_cpu_local();
    struct task *p = current;

    __asm__ ("clts");

    if (cpu->fpu_owner != p) {
        restore_fp_regs(p);
        cpu->fpu_owner = p;
    }
}
