#include <lilac/lilac.h>
#include <lilac/boot.h>

#include <asm/segments.h>
#include <asm/msr.h>
#include <asm/cpu-flags.h>

#include "idt.h"
#include "gdt.h"
#include "apic.h"
#include "timer.h"


__align(16)
unsigned char ap_stack[0x8000];

__noreturn
void ap_startup(void)
{
    u8 id = get_lapic_id();
    klog(LOG_DEBUG, "AP %d started\n", id);
    tss_init();
    set_tss_esp0((uintptr_t)ap_stack + 0x1000 * (id+1));
    load_idt();
    ap_lapic_enable();
    timer_tick_init();
    syscall_init();

    while (1)
        asm ("hlt");
}
