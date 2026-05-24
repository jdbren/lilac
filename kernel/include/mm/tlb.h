#ifndef LILAC_TLB_H
#define LILAC_TLB_H

#include <lilac/types.h>

struct mm_info;

struct tlb_inval {
    struct mm_info *mm;
    uintptr_t start, end;
    bool full;
};

int arch_tlb_flush_mmu(struct tlb_inval *tlb);

#endif
