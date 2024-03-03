#ifndef __ACLILAC_H__
#define __ACLILAC_H__

#include <kernel/types.h>

#define ACPI_MACHINE_WIDTH          BITS_PER_LONG
#define ACPI_USE_SYSTEM_CLIBRARY
#define ACPI_USE_STANDARD_HEADERS
#define ACPI_USE_NATIVE_DIVIDE

#define ACPI_CACHE_T                ACPI_MEMORY_LIST
#define ACPI_USE_LOCAL_CACHE        1

#endif
