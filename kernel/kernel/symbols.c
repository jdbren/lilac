#include <lilac/symbols.h>
#include <stddef.h>

extern const struct kernel_symbol_entry kernel_symbol_table[];
extern const size_t kernel_symbol_table_count;

const char* ksym_lookup(unsigned long addr, unsigned long *sym_addr)
{
    if (!kernel_symbol_table_count)
        return NULL;

    size_t lo = 0;
    size_t hi = kernel_symbol_table_count;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (kernel_symbol_table[mid].addr <= addr)
            lo = mid + 1;
        else
            hi = mid;
    }

    if (lo == 0)
        return NULL;

    size_t idx = lo - 1;
    if (sym_addr)
        *sym_addr = kernel_symbol_table[idx].addr;

    return kernel_symbol_table[idx].name;
}
