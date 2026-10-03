#include <lilac/lilac.h>
#include <lilac/syscall.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/signal.h>
#include <lilac/sync.h>

/*
 * Called on the way back to user mode. The flags are checked with interrupts
 * disabled so nothing can set one between the last check and the return, and
 * this returns with them still disabled; the work itself runs with them on.
 */
int do_kernel_exit_work(void)
{
    bool handler = false;
    arch_disable_interrupts();
    while (current->flags.need_resched || sigispending(current)) {
        arch_enable_interrupts();
        if (current->flags.need_resched) {
            current->flags.need_resched = 0;
            schedule();
        }
        // Default actions (a stop, an ignored signal) loop again
        if (sigispending(current) && handle_signal()) {
            handler = true;
            arch_disable_interrupts();
            break;
        }
        arch_disable_interrupts();
    }
    // No signal handler frame (a stop, an ignored signal): syscall simply restarts
    if (!handler)
        arch_restart_syscall(current, false, false);
    current->syscall_nr = -1;
    if (current->flags.restore_sigmask) {
        // sigsuspend woke but no handler ran
        current->blocked = current->saved_sigmask;
        current->flags.restore_sigmask = 0;
    }
    return 0;
}
