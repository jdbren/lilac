#include <lilac/lilac.h>

#include <asm/segments.h>
#include <asm/msr.h>
#include <asm/cpu-flags.h>

u8 get_lapic_id(void);

__align(16)
unsigned char ap_stack[0x8000];

// Begin the fun of multiprocessing (need to add locks everywhere)
__noreturn
void ap_startup(void)
{
    u8 id = get_lapic_id();
    klog(LOG_DEBUG, "AP %d started\n", id);
    while (1)
        asm volatile ("hlt");
}
