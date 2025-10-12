#ifndef __ACLILAC_H__
#define __ACLILAC_H__

#include <lilac/types.h>
#include <lilac/sync.h>
#include <lilac/log.h>
#include <lilac/libc.h>

#define ACPI_MACHINE_WIDTH          BITS_PER_LONG
#define ACPI_USE_SYSTEM_CLIBRARY
#define ACPI_USE_NATIVE_DIVIDE
#define ACPI_USE_SYSTEM_INTTYPES
//#define ACPI_USE_STANDARD_HEADERS

#define ACPI_CACHE_T                ACPI_MEMORY_LIST
#define ACPI_USE_LOCAL_CACHE        1

#define ACPI_SEMAPHORE              sem_t*
#define ACPI_SPINLOCK               spinlock_t*

// #define ACPI_DEBUG_OUTPUT
// #define ACPI_NO_ERROR_MESSAGES

#define ACPI_MSG_INFO           "ACPI: "
#define ACPI_MSG_WARNING        "ACPI Warning: "
#define ACPI_MSG_ERROR          "ACPI Error: "

#if BITS_PER_LONG == 64
#define COMPILER_DEPENDENT_INT64    long
#define COMPILER_DEPENDENT_UINT64   unsigned long
#else
#define COMPILER_DEPENDENT_INT64    long long
#define COMPILER_DEPENDENT_UINT64   unsigned long long
#endif

typedef uint64_t UINT64;
typedef int64_t INT64;
#ifdef __x86_64__
typedef uint32_t UINT32;
typedef int32_t INT32;
#else
typedef long unsigned int UINT32;
typedef long int INT32;
#endif
typedef uint16_t UINT16;
typedef int16_t INT16;
typedef uint8_t UINT8;
typedef int8_t INT8;
typedef uint8_t BOOLEAN;
typedef void VOID;

#endif /* __ACLILAC_H__ */
