// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <mm/kmm.h>
#include <lilac/math.h>
#include <lilac/panic.h>
#include <asm/regs.h>
#include <asm/cpu-flags.h>
#include <asm/msr.h>
#include <asm/cpu.h>
#include <asm/cpu.h>
#include "paging.h"

uintptr_t __walk_pages(void *vaddr)
{
    return (uintptr_t)__get_physaddr(vaddr);
}

int x86_to_page_flags(int flags)
{
    int result = 0;
    if (flags & MEM_PF_WRITE)
        result |= PG_WRITE;
    if (flags & MEM_PF_USER)
        result |= PG_USER;
    if (flags & MEM_PF_NO_CACHE)
        result |= PG_CACHE_DISABLE;
    if (flags & MEM_PF_CACHE_WT)
        result |= PG_WRITE_THROUGH;
    if (flags & MEM_PF_GLOBAL)
        result |= PG_GLOBAL;
    if (flags & MEM_PF_UC)
        result |= (PG_CACHE_DISABLE | PG_WRITE_THROUGH);
    return result;
}

uintptr_t arch_get_pgd(void)
{
    uintptr_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}


void mtrr_enable(void)
{
    /* Flush caches and TLB */
    wbinvd();
    __native_flush_tlb();

    /* Enable MTRRs */
    u64 def = rdmsr(IA32_MTRR_DEF_TYPE);
    def |= (1ULL << 11);   // E = 1 (enable MTRRs)
    wrmsr(IA32_MTRR_DEF_TYPE, def);

    /* Enable caches */
    unsigned long cr0 = read_cr0();
    cr0 &= ~X86_CR0_CD;  // CR0.CD = 0
    cr0 &= ~X86_CR0_NW;  // CR0.NW = 0
    write_cr0(cr0);
}

void mtrr_disable(void)
{
    /* Disable caching */
    unsigned long cr0 = read_cr0();
    cr0 |= X86_CR0_CD;   // CR0.CD = 1
    cr0 &= ~X86_CR0_NW;  // CR0.NW = 0
    write_cr0(cr0);

    /* Flush caches and TLB */
    wbinvd();
    __native_flush_tlb();

    /* Disable MTRRs */
    u64 def = rdmsr(IA32_MTRR_DEF_TYPE);
    def &= ~(1ULL << 11);   // E = 0 (disable MTRRs)
    wrmsr(IA32_MTRR_DEF_TYPE, def);
}

void mtrr_dump(void)
{
    u64 cap = rdmsr(IA32_MTRR_CAP);
    u64 def = rdmsr(IA32_MTRR_DEF_TYPE);
    unsigned var_count = cap & 0xFF;

    klog(LOG_INFO, "MTRR Dump:\n");
    klog(LOG_INFO, " IA32_MTRR_CAP: 0x%lx\n", cap);
    klog(LOG_INFO, " IA32_MTRR_DEF_TYPE: 0x%lx\n", def);
    for (unsigned i = 0; i < var_count; i++) {
        u64 base = rdmsr(IA32_MTRR_PHYSBASEn(i));
        u64 mask = rdmsr(IA32_MTRR_PHYSMASKn(i));
        klog(LOG_INFO, " MTRR %d: Base=0x%lx, Mask=0x%lx\n", i, base, mask);
    }
}

static int find_free_mtrr(void)
{
    u64 cap = rdmsr(IA32_MTRR_CAP);
    unsigned var_count = cap & 0xFF;

    for (unsigned i = 0; i < var_count; i++) {
        u64 mask = rdmsr(IA32_MTRR_PHYSMASKn(i));
        if ((mask & (1ULL << 11)) == 0)
            return i;
    }
    return -1;
}

static u64 get_phys_mask(void)
{
    u32 eax, ebx, ecx, edx;
    if (max_cpuid_leaf < 0x80000008)
        return (1ull << 36) - 1;
    cpuid(0x80000008, &eax, &ebx, &ecx, &edx);
    return (1ULL << (eax & 0xff)) - 1;
}

static void mtrr_get_region(int idx, u64 *base_out, u64 *size_out, u8 *type_out)
{
    u64 phys_mask = get_phys_mask();
    u64 base = rdmsr(IA32_MTRR_PHYSBASEn(idx));
    u64 mask = rdmsr(IA32_MTRR_PHYSMASKn(idx));

    *type_out = base & 0xFF;
    *base_out = base & phys_mask & ~0xFFFULL;

    if (!(mask & (1ULL << 11))) {
        *size_out = 0;  /* MTRR not valid */
        return;
    }

    /* Size = ~(mask & phys_mask) + 1, masked to valid bits */
    u64 mask_val = mask & phys_mask & ~0xFFFULL;
    *size_out = (~mask_val & phys_mask) + 1;
}

static void mtrr_set_region(int idx, u64 base, u64 size, u8 type)
{
    u64 phys_mask = get_phys_mask();
    u64 base_mask = phys_mask & ~0xFFFULL;

    wrmsr(IA32_MTRR_PHYSMASKn(idx), 0);  // Disable MTRR first
    if (size == 0)
        return;

    wrmsr(IA32_MTRR_PHYSBASEn(idx), (base & base_mask) | type);
    wrmsr(IA32_MTRR_PHYSMASKn(idx), (~(size - 1) & phys_mask) | (1ULL << 11));
}

/* Returns the largest power-of-2 size that fits at this base address */
static u64 largest_pow2_region(uintptr_t base, u64 size)
{
    /* Find largest power of 2 <= size */
    u64 pow2_size = 1ULL << (63 - __builtin_clzl(size));

    /* Ensure alignment: base must be aligned to pow2_size */
    while (pow2_size > 0 && (base & (pow2_size - 1)) != 0)
        pow2_size >>= 1;

    return pow2_size;
}

/* Set multiple MTRRs to cover a non-power-of-2 region */
static void mtrr_set_region_multi(u64 base, u64 size, u8 type)
{
    while (size > 0) {
        u64 chunk = largest_pow2_region(base, size);
        if (chunk == 0)
            break;

        int idx = find_free_mtrr();
        if (idx < 0) {
            klog(LOG_WARN, "No free MTRRs for region base=0x%lx size=0x%lx\n", base, size);
            break;
        }

        klog(LOG_DEBUG, "  Setting MTRR %d: base=0x%lx size=0x%lx type=%d\n",
             idx, base, chunk, type);
        mtrr_set_region(idx, base, chunk, type);

        base += chunk;
        size -= chunk;
    }
}

static int find_overlapping_mtrr(u64 fb_base, u64 fb_size)
{
    u64 cap = rdmsr(IA32_MTRR_CAP);
    unsigned var_count = cap & 0xFF;
    u64 fb_end = fb_base + fb_size;

    for (unsigned i = 0; i < var_count; i++) {
        u64 base, size;
        u8 type;
        mtrr_get_region(i, &base, &size, &type);

        if (size == 0)
            continue;

        u64 end = base + size;
        if (fb_base < end && fb_end > base)
            return i;
    }
    return -1;
}

void mmio_map_buffer_wc(uintptr_t paddr, size_t sizen)
{
    const u64 size = is_pow_2(sizen) ? sizen : next_pow_2(sizen);
    u64 fb_base = paddr & ~(size - 1);
    u64 fb_end = fb_base + size;

    mtrr_disable();

    /* Check for overlapping MTRR and split if needed */
    int overlap_idx = find_overlapping_mtrr(fb_base, size);
    if (overlap_idx >= 0) {
        u64 orig_base, orig_size;
        u8 orig_type;
        mtrr_get_region(overlap_idx, &orig_base, &orig_size, &orig_type);
        u64 orig_end = orig_base + orig_size;

        klog(LOG_DEBUG, "MTRR %d overlaps FB region, splitting\n", overlap_idx);
        klog(LOG_DEBUG, "  Original: base=0x%lx size=0x%lx type=%d\n",
             orig_base, orig_size, orig_type);

        /* Clear the original MTRR */
        mtrr_set_region(overlap_idx, 0, 0, 0);

        /* Region before framebuffer - may need multiple MTRRs */
        if (fb_base > orig_base) {
            u64 pre_size = fb_base - orig_base;
            klog(LOG_DEBUG, "  Pre-region: base=0x%lx size=0x%lx\n",
                 orig_base, pre_size);
            mtrr_set_region_multi(orig_base, pre_size, orig_type);
        }

        /* Region after framebuffer - may need multiple MTRRs */
        if (orig_end > fb_end) {
            u64 post_size = orig_end - fb_end;
            klog(LOG_DEBUG, "  Post-region: base=0x%lx size=0x%lx\n",
                 fb_end, post_size);
            mtrr_set_region_multi(fb_end, post_size, orig_type);
        }
    }

    /* Now set the WC MTRR for framebuffer */
    int n = find_free_mtrr();
    if (n < 0) {
        klog(LOG_WARN, "No free MTRRs for framebuffer region\n");
        goto out;
    }

    klog(LOG_DEBUG, "Setting MTRR %d for FB: base=0x%lx size=0x%lx type=WC\n",
         n, fb_base, size);
    mtrr_set_region(n, fb_base, size, MTRR_TYPE_WC);

out:
    mtrr_enable();
    mtrr_dump();
}
