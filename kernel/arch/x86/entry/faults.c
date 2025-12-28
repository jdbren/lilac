// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/lilac.h>
#include <lilac/signal.h>
#include <lilac/sched.h>
#include <mm/mm.h>
#include <asm/idt.h>
#include "paging.h"

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
    if (err_ip <= __USER_MAX_ADDR)
        return NULL;
    const int num_entries = EXCEPTION_TABLE_SIZE(&_exception_start, &_exception_end);
    for (int i = 0; i < num_entries; i++) {
        if (exception_table[i].err_addr == err_ip)
            return exception_table[i].handler;
    }
    return NULL;
}

void div0_handler(struct interrupt_frame *frame)
{
    if (frame->ip < __USER_STACK) { // user space fault
        klog(LOG_WARN, "Divide by zero in user space at %p, sending SIGFPE\n", frame->ip);
        do_raise(current, SIGFPE);
    } else {
        kerror("Divide by zero int in kernel\n");
    }
}


static unsigned long get_fault_flags(long err_code)
{
    unsigned long flags = 0;
    if (err_code & X86_FAULT_PRESENT)
        flags |= FAULT_PTE_EXIST;
    if (err_code & X86_FAULT_WRITE)
        flags |= FAULT_WRITE;
    if (err_code & X86_FAULT_USER)
        flags |= FAULT_USER;
    if (err_code & X86_FAULT_INSTR)
        flags |= FAULT_INSTR;
    return flags;
}

static int user_page_fault(long error, struct interrupt_frame *frame, uintptr_t addr)
{
    struct vm_desc *vma = find_vma(current->mm, addr);
    if (!vma) {
        do_raise(current, SIGSEGV);
        return -1;
    }
#if defined DEBUG_VMA || defined DEBUG_MM
    klog(LOG_DEBUG, "Found VMA %lx-%lx for faulting address %lx\n", vma->start, vma->end, addr);
    klog(LOG_DEBUG, "VMA flags: %x\n", vma->vm_flags);
#endif
    int fault_ret = mm_fault(vma, addr, get_fault_flags(error));

    if (fault_ret == FAULT_PROT_VIOLATION) {
        do_raise(current, SIGSEGV);
        return -1;
    } else if (fault_ret == FAULT_FILE_ERROR) {
        do_raise(current, SIGBUS);
        return -1;
    }

    return 0;
}

void pgflt_handler(long error_code, struct interrupt_frame *frame)
{
    uintptr_t addr = 0;
    asm volatile ("mov %%cr2,%0\n\t" : "=r"(addr));
#ifdef DEBUG_MM
    klog(LOG_DEBUG, "Page fault at %p, Code: %lx, IP: %p\n", addr, error_code, frame->ip);
#endif
    void *handler = find_exception(frame->ip);
    const bool user_addr = addr < __USER_MAX_ADDR && addr > 0x1000;

    if (error_code & X86_FAULT_USER) {
        if (!user_addr) {
            do_raise(current, SIGSEGV);
        } else {
            user_page_fault(error_code, frame, addr);
        }
    } else if (user_addr) {
        user_page_fault(error_code, frame, addr);
    } else if (handler && addr < __KERNEL_BASE) {
        klog(LOG_DEBUG, "Page fault at %x handled by %p\n", frame->ip, handler);
        frame->ip = (uintptr_t)handler;
    } else {
        panic("Kernel page fault at %p (code %lx, cr2 %p)\n", (void*)frame->ip, error_code, (void*)addr);
    }
}

void gpflt_handler(long error_code, struct interrupt_frame *frame)
{
    uintptr_t addr = 0;
    asm volatile ("mov %%cr2,%0\n\t" : "=r"(addr));
    klog(LOG_WARN, "General protection fault at %p, error code %x\n", frame->ip, error_code);
    void *handler = find_exception(frame->ip);
    if (handler && addr < __KERNEL_BASE) {
        do_raise(current, SIGSEGV);
        frame->ip = (uintptr_t)handler;
    } else if (frame->ip < __USER_STACK) { // user space fault
        klog(LOG_WARN, "GP fault in user space at %p, sending SIGSEGV\n", frame->ip);
        do_raise(current, SIGSEGV);
    } else {
        kerror("General protection fault detected\n");
    }
}

void invldop_handler(struct interrupt_frame *frame)
{
    klog(LOG_WARN, "Invalid opcode at %p\n", frame->ip);
    if (frame->ip < __USER_STACK) { // user space fault
        klog(LOG_WARN, "Invalid opcode in user space at %p, sending SIGILL\n", frame->ip);
        do_raise(current, SIGILL);
    } else {
        kerror("Invalid opcode detected\n");
    }
}

void dblflt_handler(long error_code, struct interrupt_frame *frame)
{
    kerror("Double fault detected\n");
}

void debug_handler(struct interrupt_frame *frame)
{
    klog(LOG_INFO, "Debug exception at %p\n", frame->ip);
    // For now, just ignore it.
}

void nmi_handler(struct interrupt_frame *frame)
{
    klog(LOG_WARN, "Non-maskable interrupt at %p\n", frame->ip);
    kerror("NMI detected\n");
}

void brkp_handler(struct interrupt_frame *frame)
{
    klog(LOG_INFO, "Breakpoint at %p\n", frame->ip);
}

void ovflw_handler(struct interrupt_frame *frame)
{
    if (frame->ip < __USER_STACK) { // user space fault
        klog(LOG_WARN, "Overflow in user space, sending SIGFPE\n");
        do_raise(current, SIGFPE);
    } else {
        kerror("Overflow detected\n");
    }
}

void bnd_handler(struct interrupt_frame *frame)
{
    if (frame->ip < __USER_STACK) { // user space fault
        klog(LOG_WARN, "BOUND range exceeded in user space, sending SIGSEGV\n");
        do_raise(current, SIGSEGV);
    } else {
        kerror("BOUND range exceeded detected\n");
    }
}

void dna_handler(struct interrupt_frame *frame)
{
    kerror("Device not available (FPU) fault detected\n");
}

void invldtss_handler(long error_code, struct interrupt_frame *frame)
{
    klog(LOG_WARN, "Invalid TSS at %p, error code %x\n", frame->ip, error_code);
    kerror("Invalid TSS detected\n");
}

void segnp_handler(long error_code, struct interrupt_frame *frame)
{
    kerror("Segment not present fault detected\n");
}

void ssflt_handler(long error_code, struct interrupt_frame *frame)
{
    kerror("Stack segment fault detected\n");
}

void flpexc_handler(struct interrupt_frame *frame)
{
    if (frame->ip < __USER_STACK) { // user space fault
        klog(LOG_WARN, "Floating point exception in user space at %p, sending SIGFPE\n", frame->ip);
        do_raise(current, SIGFPE);
    } else {
        kerror("Floating point exception detected\n");
    }
}

void align_handler(long error_code, struct interrupt_frame *frame)
{
    klog(LOG_WARN, "Alignment check fault at %p, error code %x\n", frame->ip, error_code);
    if (frame->ip < __USER_STACK) { // user space fault
        klog(LOG_WARN, "Alignment check in user space at %p, sending SIGBUS\n", frame->ip);
        do_raise(current, SIGBUS);
    } else {
        kerror("Alignment check fault detected\n");
    }
}

void mchk_handler(struct interrupt_frame *frame)
{
    kerror("Machine check exception detected\n");
}

void simd_handler(struct interrupt_frame *frame)
{
    if (frame->ip < __USER_STACK) { // user space fault
        klog(LOG_WARN, "SIMD floating point exception in user space at %p, sending SIGFPE\n", frame->ip);
        do_raise(current, SIGFPE);
    } else {
        kerror("SIMD floating point exception detected\n");
    }
}
