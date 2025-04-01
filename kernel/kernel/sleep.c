#include <lilac/config.h>
#include <lilac/log.h>
#include <lilac/syscall.h>
#include <lilac/timer.h>
#include <lilac/uaccess.h>
#include <acpi/acpica.h>

void arch_shutdown(void);

__noreturn void kernel_shutdown(void)
{
    klog(LOG_INFO, "Shutting down\n");
    arch_shutdown();
    unreachable();
}

__noreturn SYSCALL_DECL0(shutdown)
{
    kernel_shutdown();
}

SYSCALL_DECL1(time, long long *, t)
{
    struct task *p = current;
    long long ret = get_unix_time();

    if (t) {
        if (access_ok(t, sizeof(long long), p->mm))
            *t = ret;
        else
            ret = -EFAULT;
    }

    return ret;
}
