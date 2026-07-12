#include <lilac/panic.h>
#include <lilac/console.h>
#include <lilac/interrupt.h>
#include <lilac/log.h>
#include <lilac/libc.h>
#include <lilac/percpu.h>
#include <lilac/timer.h>
#include <drivers/framebuffer.h>

__noreturn void kerror(const char *msg, ...)
{
    static volatile atomic_bool panic_mode = false;

	arch_disable_interrupts();
    va_list args;
	console_write_screen(1);
    u64 stime = ktime_get();

    if (!panic_mode) {
        arch_broadcast_others_ipi(HALT_CPU_VECTOR);
        panic_mode = true;
    }

    klog_lock();
    printf("[%4lld.%09lld] ", (long long)(stime / 1000000000ll),
        (long long)(stime % 1000000000ll));
    putchar('[');
	graphics_setcolor(RGB_RED, RGB_BLACK);
	printf(" PANIC ");
    graphics_setcolor(RGB_WHITE, RGB_BLACK);
    putchar(']');
    printf(" (cpu #%d) ", this_cpu_id());

	va_start(args, msg);
	vprintf(msg, args);
	va_end(args);

    if (!msg || strlen(msg) == 0 || msg[strlen(msg) - 1] != '\n')
        putchar('\n');

    arch_panic_dump_regs();
    arch_panic_stack_trace();
    klog_unlock();

    arch_ipi_send_self(HALT_CPU_VECTOR);
    for (;;)
        arch_halt_cpu();
}
