#ifndef _LILAC_PERCPU_H
#define _LILAC_PERCPU_H

#include <lilac/compiler.h>
#include <lilac/config.h>
#include <lilac/types.h>

#define PERCPU_ALIGN 64
#define PERCPU_SECTION ".data.percpu"
#define __percpu __section(PERCPU_SECTION) __align(PERCPU_ALIGN)


struct __align(64) cpu_local {
    unsigned long id;
    long reserved;
    void *priv; // architecture specific data
    void *user_stack;
};

static inline struct cpu_local * this_cpu_local(void)
{
    struct cpu_local *ptr;
    __asm__ ("mov %%gs:0, %0" : "=r"(ptr));
    return ptr;
}

#define this_cpu_id() (this_cpu_local()->id)


extern uintptr_t __per_cpu_offset[CONFIG_MAX_CPUS];

#define DEFINE_PER_CPU(type, name) \
    __percpu __typeof__(type) __per_cpu_##name

#define per_cpu_offset(x) (__per_cpu_offset[(x)])

#define shift_percpu_ptr(ptr, offset) \
    ((__typeof__(*(ptr)) *)((uintptr_t)(ptr) + (offset)))

#define per_cpu_ptr(var, cpu) \
    (shift_percpu_ptr(&__per_cpu_##var, per_cpu_offset(cpu)))

#define this_cpu_ptr(var) per_cpu_ptr(var, this_cpu_id())


void percpu_mem_init(void);
void percpu_init_cpu(void);


#endif
