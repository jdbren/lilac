// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
/*
    ACPICA OS Layer
*/

#include <kernel/lilac.h>
#include <kernel/types.h>
#include <kernel/config.h>
#include <mm/kmm.h>
#include <mm/kheap.h>
#include "acpica/acpi.h"

ACPI_STATUS AcpiOsInitialize() { return AE_OK; }

ACPI_STATUS AcpiOsTerminate() { return AE_OK; }

ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer()
{
    return (ACPI_PHYSICAL_ADDRESS)get_rsdp();
}

ACPI_STATUS AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES *PredefinedObject,
    ACPI_STRING *NewValue)
{
    *NewValue = NULL;
    return AE_OK;
}

ACPI_STATUS AcpiOsTableOverride(ACPI_TABLE_HEADER *ExistingTable,
    ACPI_TABLE_HEADER **NewTable)
{
    *NewTable = NULL;
    return AE_OK;
}


void *AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS PhysicalAddress, ACPI_SIZE Length)
{
    void *ptr = map_phys((void*)PhysicalAddress, Length, PG_WRITE);
    int offset = PhysicalAddress & 0xFFF;
    return (void*)((u32)ptr + offset);
}

void AcpiOsUnmapMemory(void *where, ACPI_SIZE length)
{
    unmap_phys(where, length / PAGE_SIZE);
}

ACPI_STATUS AcpiOsGetPhysicalAddress(void *LogicalAddress,
    ACPI_PHYSICAL_ADDRESS *PhysicalAddress)
{
    *PhysicalAddress = (ACPI_PHYSICAL_ADDRESS)virt_to_phys(LogicalAddress);
    return AE_OK;
}

void *AcpiOsAllocate(ACPI_SIZE Size)
{
    return kmalloc(Size);
}

void AcpiOsFree(void *Memory)
{
    kfree(Memory);
}

BOOLEAN AcpiOsReadable(void *Memory, ACPI_SIZE Length)
{
    return TRUE;
}

BOOLEAN AcpiOsWritable(void *Memory, ACPI_SIZE Length)
{
    return TRUE;
}

// Threads

// Mutexes, locks, and semaphores
ACPI_STATUS AcpiOsCreateLock(ACPI_SPINLOCK *OutHandle)
{
    if (!OutHandle)
        return AE_BAD_PARAMETER;
    *OutHandle = create_lock();
    return AE_OK;
}

void AcpiOsDeleteLock(ACPI_HANDLE Handle)
{
    delete_lock(Handle);
}

ACPI_CPU_FLAGS AcpiOsAcquireLock(ACPI_SPINLOCK Handle)
{
    acquire_lock(Handle);
    return AE_OK;
}

void AcpiOsReleaseLock(ACPI_SPINLOCK Handle, ACPI_CPU_FLAGS Flags)
{
    release_lock(Handle);
}

//Interrupts
ACPI_STATUS AcpiOsInstallInterruptHandler (UINT32 InterruptLevel,
    ACPI_OSD_HANDLER Handler, void *Context)
{
    
    return AE_OK;
}