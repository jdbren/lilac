// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/panic.h>
#include <lilac/log.h>
#include <lilac/types.h>
#include <lilac/config.h>

#include "idt.h"

#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wanalyzer-out-of-bounds"
#pragma GCC diagnostic ignored "-Wanalyzer-allocation-size"

struct exception_entry {
    uintptr_t err_addr;
    void *handler;
};

#define EXCEPTION_TABLE_SIZE(start, end) \
    ((uintptr_t)(end) - (uintptr_t)(start)) / sizeof(struct exception_entry)

extern const u32 _exception_start, _exception_end;
static struct exception_entry *const
    exception_table = (struct exception_entry*)&_exception_start;

static void * find_exception(uintptr_t err_ip)
{
    const int num_entries = EXCEPTION_TABLE_SIZE(&_exception_start, &_exception_end);
    for (int i = 0; i < num_entries; i++) {
        klog(LOG_DEBUG, "Checking %x against %x\n", err_ip, exception_table[i].err_addr);
        if (exception_table[i].err_addr == err_ip)
            return exception_table[i].handler;
    }
    return NULL;
}

void div0_handler(void)
{
    kerror("Divide by zero int\n");
}

void pgflt_handler(long error_code, struct interrupt_frame *frame)
{
    uintptr_t addr = 0;
    asm volatile ("mov %%cr2,%0\n\t" : "=r"(addr));

    klog(LOG_WARN, "Fault address: %x\n", addr);
    klog(LOG_WARN, "Error code: %x\n", error_code);

    void *handler = find_exception(frame->ip);
    if (handler && addr < __KERNEL_BASE) {
        klog(LOG_WARN, "Page fault at %x handled by %p\n", frame->ip, handler);
#ifdef __x86_64__
        asm (
            "movq (%%rbp), %%rax\n\t"   // Get previous fp
            "movq %0, 16(%%rax)\n\t"  // Overwrite return address with handler
            : :"r"(handler) : "rax", "memory"
        );
#else
        asm (
            "movl (%%ebp), %%eax\n\t"   // Get previous fp
            "movl %0, 8(%%eax)\n\t"  // Overwrite return address with handler
            : :"r"(handler) : "eax", "memory"
        );
#endif
    } else {
        kerror("Page fault detected\n");
    }
}

void gpflt_handler(int error_code)
{
    klog(LOG_WARN, "Error code: %x\n", error_code);
    kerror("General protection fault detected\n");
}

void invldop_handler(void)
{
    kerror("Invalid opcode detected\n");
}

void dblflt_handler(void)
{
    kerror("Double fault detected\n");
}
