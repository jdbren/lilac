#ifndef __ACLILAC_H__
#define __ACLILAC_H__

#include <lilac/types.h>
#include <lilac/sync.h>
#include <lilac/log.h>

#define ACPI_MACHINE_WIDTH          BITS_PER_LONG
//#define ACPI_USE_SYSTEM_INTTYPES
#define ACPI_USE_SYSTEM_CLIBRARY
#define ACPI_USE_STANDARD_HEADERS
#define ACPI_USE_NATIVE_DIVIDE

#define ACPI_CACHE_T                ACPI_MEMORY_LIST
#define ACPI_USE_LOCAL_CACHE        1

#define ACPI_SEMAPHORE              sem_t*
#define ACPI_SPINLOCK               spinlock_t*

// #define ACPI_DEBUG_OUTPUT
// #define ACPI_NO_ERROR_MESSAGES

#endif
