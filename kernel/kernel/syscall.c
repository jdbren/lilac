#include <lilac/lilac.h>
#include <lilac/syscall.h>
#include <lilac/process.h>
#include <lilac/signal.h>

int do_kernel_exit_work(void)
{
    if (current->flags.need_resched) {
        current->flags.need_resched = 0;
        yield();
    }
    if (current->flags.sig_pending) {
        klog(LOG_DEBUG, "Handling pending signals\n");
        handle_signals();
        current->flags.sig_pending = 0;
    }
    return 0;
}
