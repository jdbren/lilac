#pragma once

struct kernel_symbol_entry {
    unsigned long addr;
    const char *name;
};

const char* ksym_lookup(unsigned long addr, unsigned long *sym_addr);
