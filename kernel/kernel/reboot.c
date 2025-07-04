#include <lilac/config.h>
#include <lilac/lilac.h>
#include <lilac/log.h>
#include <lilac/syscall.h>
#include <lilac/timer.h>
#include <lilac/uaccess.h>
#include <lilac/port.h>
#include <acpi/acpica.h>

void arch_shutdown(void);

__noreturn void kernel_shutdown(void)
{
    klog(LOG_INFO, "Shutting down\n");
    arch_shutdown();
    unreachable();
}

/*
__noreturn void kernel_reboot(void)
{
    //klog(LOG_INFO, "Rebooting system\n");
    AcpiReset();
    // if that failed keyboard reset the CPU
    // klog(LOG_ERROR, "ACPI reset failed, doing cpu reset\n");
    // uint8_t good = 0x02;
    // while (good & 0x02)
    //     good = read_port(0x64, 8);
    // write_port(0x64, 0xFE, 8);
    while (1)
        asm ("hlt");
    unreachable();
}
*/

SYSCALL_DECL1(reboot, unsigned long, how)
{
    for (;;) {
        arch_disable_interrupts();
        switch (how) {
            // case 0:
            //     kernel_reboot();
            //     break;
            case 1:
                kernel_shutdown();
                break;
            default:
                klog(LOG_ERROR, "Invalid reboot option: %lu\n", how);
                return -EINVAL;
        }
    }
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
