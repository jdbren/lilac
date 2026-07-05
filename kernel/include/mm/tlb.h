#ifndef LILAC_TLB_H
#define LILAC_TLB_H

#include <lilac/types.h>

struct task;
struct mm_info;

struct tlb_inval {
    struct mm_info *mm;
    uintptr_t start, end;
    bool full;
};

void init_tlb_shootdown(void);
void tlb_shootdown(struct tlb_inval *tlb, struct task *task);

int arch_tlb_flush_mmu(struct tlb_inval *tlb);

#endif
