#include <kernel/kmain.h>

void kmain()
{
    while (1) {
        asm("hlt");
    }
}
