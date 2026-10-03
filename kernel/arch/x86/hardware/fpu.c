#include <lilac/sched.h>
#include <lilac/percpu.h>
#include <asm/cpu.h>
#include <asm/regs.h>

#define FXSAVE_BYTES 512
#define FXSAVE_MXCSR 24
#define FXSAVE_MXCSR_MASK 28
#define MXCSR_MASK_DEFAULT 0xffbf

// Power-on state: x87 control word 0x37f, all exceptions masked in MXCSR
static const u8 fx_init_state[FXSAVE_BYTES] __attribute__((aligned(16))) = {
    [0] = 0x7f, [1] = 0x03,
    [FXSAVE_MXCSR] = 0x80, [FXSAVE_MXCSR + 1] = 0x1f,
};

void save_fp_regs(struct task *p)
{
    if (!p->fp_regs) {
        p->fp_regs = kzmalloc(FXSAVE_BYTES);
        if (!p->fp_regs)
            panic("Out of memory allocating FP state");
    }

    __builtin_ia32_fxsave64(p->fp_regs);
}

void restore_fp_regs(struct task *p)
{
    __builtin_ia32_fxrstor64(p->fp_regs ? p->fp_regs : (void*)fx_init_state);
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

// Give up this cpu's live copy of p's state; the next FP use reloads p->fp_regs
static void fpu_drop_owner(struct task *p)
{
    unsigned long flags;
    arch_local_irq_save(&flags);
    arch_disable_interrupts();
    struct cpu_local *cpu = this_cpu_local();
    if (cpu->fpu_owner == p) {
        cpu->fpu_owner = NULL;
        write_cr0(read_cr0() | X86_CR0_TS);
    }
    arch_local_irq_restore(flags);
}

/*
 * Signal delivery. The interrupted FP state is copied into the signal frame
 * and the handler starts from the init state, so it can't disturb the
 * interrupted code's registers. sigreturn copies the frame's state back.
 */

// Current FP state of p, synced from the cpu if p owns it
const void *fpu_sigframe_state(struct task *p)
{
    unsigned long flags;
    arch_local_irq_save(&flags);
    arch_disable_interrupts();
    if (this_cpu_local()->fpu_owner == p)
        save_fp_regs(p);
    arch_local_irq_restore(flags);
    return p->fp_regs ? p->fp_regs : fx_init_state;
}

void fpu_sigframe_reset(struct task *p)
{
    fpu_drop_owner(p);
    if (p->fp_regs)
        memcpy(p->fp_regs, fx_init_state, FXSAVE_BYTES);
}

// Buffer for sigreturn to copy the saved state into, or NULL if out of memory
void *fpu_sigframe_restore_buf(struct task *p)
{
    fpu_drop_owner(p);
    if (!p->fp_regs)
        p->fp_regs = kzmalloc(FXSAVE_BYTES);
    return p->fp_regs;
}

// The state came from user memory; an invalid MXCSR would #GP on fxrstor
void fpu_sigframe_restore_fixup(struct task *p)
{
    u32 *mxcsr = (u32*)((u8*)p->fp_regs + FXSAVE_MXCSR);
    *mxcsr &= MXCSR_MASK_DEFAULT;
}
