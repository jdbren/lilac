#include <lilac/symbols.h>
#include <stddef.h>

__attribute__((weak)) const struct kernel_symbol_entry kernel_symbol_table[] = {
    { 0, NULL }
};

__attribute__((weak)) const size_t kernel_symbol_table_count = 0;
