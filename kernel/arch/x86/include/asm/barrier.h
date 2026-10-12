#ifndef ASM_X86_BARRIER_H
#define ASM_X86_BARRIER_H

#define barrier() asm volatile ("" ::: "memory")

#if defined(__i386__)
/*
 * Some non-Intel clones support out of order store. wmb() ceases to be a
 * nop for these.
 */
#define mb()        asm volatile("lock; addl $0,0(%%esp)" ::: "memory")
#define rmb()       asm volatile("lock; addl $0,0(%%esp)" ::: "memory")
#define wmb()       asm volatile("lock; addl $0,0(%%esp)" ::: "memory")
#elif defined(__x86_64__)
#define mb()        asm volatile("mfence" ::: "memory")
#define rmb()       asm volatile("lfence" ::: "memory")
#define wmb()       asm volatile("sfence" ::: "memory")
#endif

#define smp_rmb()   barrier()
#define smp_wmb()   barrier()
#define smp_mb()    asm volatile("lock; addl $0,-4(%%rsp)" ::: "memory", "cc")

#define smp_store_mb(var, value) do { (void)__atomic_exchange_n(&var, value, __ATOMIC_SEQ_CST); } while (0)

#define smp_store_release(p, v) \
do {                            \
    barrier();                  \
    WRITE_ONCE(*p, v);          \
} while (0)

#define smp_load_acquire(p)             \
({                                      \
    typeof(*p) ___p1 = READ_ONCE(*p);   \
    barrier();                          \
    ___p1;                              \
})

#endif
