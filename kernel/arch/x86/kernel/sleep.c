#include <acpi/acpica.h>

void arch_shutdown(void)
{
    ACPI_STATUS status = AcpiEnterSleepStatePrep(5);
    if (ACPI_FAILURE(status))
        kerror("Error preparing to enter sleep state\n");
    AcpiEnterSleepState(5);
}
