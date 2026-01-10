#include <lilac/lilac.h>
#include <lilac/syscall.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/signal.h>

int do_kernel_exit_work(void)
{
    while (current->flags.need_resched || current->flags.sig_pending) {
        if (current->flags.need_resched) {
            current->flags.need_resched = 0;
            schedule();
        }
        if (current->flags.sig_pending) {
            klog(LOG_DEBUG, "Handling first pending signal\n");
            handle_signal();
            break;
        }
    }
    return 0;
}
