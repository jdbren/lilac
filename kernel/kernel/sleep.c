#include <lilac/config.h>
#include <acpi/acpica.h>

__noreturn void kernel_shutdown(void)
{
    klog(LOG_INFO, "Shutting down\n");
    arch_shutdown();
}
