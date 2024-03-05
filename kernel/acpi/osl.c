// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
/*
    ACPICA OS Layer
*/

#include <kernel/lilac.h>
#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/interrupt.h>
#include <kernel/io.h>
#include <kernel/process.h>
#include <kernel/timer.h>
#include <mm/kmm.h>
#include <mm/kheap.h>
#include "acpica/acpi.h"

ACPI_OSD_HANDLER acpi_isr;
void *acpi_isr_context;

struct interrupt_frame;

void ISR AcpiInt(struct interrupt_frame *frame)
{
    (*acpi_isr)(acpi_isr_context);
}

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
    unmap_phys(where, length);
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
ACPI_THREAD_ID AcpiOsGetThreadId()
{
    return get_pid();
}

void AcpiOsSleep(UINT64 Milliseconds)
{
    sleep(Milliseconds);
}

void AcpiOsStall(UINT32 Microseconds)
{
    sleep(Microseconds);
}

// Mutexes, locks, and semaphores
ACPI_STATUS AcpiOsCreateSemaphore(UINT32 MaxUnits, UINT32 InitialUnits, ACPI_SEMAPHORE *OutHandle)
{
    if (!OutHandle)
        return AE_BAD_PARAMETER;
    *OutHandle = kmalloc(sizeof(sem_t));
    sem_init(*OutHandle, InitialUnits);
    return AE_OK;
}

ACPI_STATUS AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle)
{
    if (!Handle)
        return AE_BAD_PARAMETER;
    kfree(Handle);
    return AE_OK;
}

ACPI_STATUS AcpiOsWaitSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units, UINT16 Timeout)
{
    if (!Handle)
        return AE_BAD_PARAMETER;
    if (Timeout == ACPI_WAIT_FOREVER)
        while (Units--)
            sem_wait(Handle);
    else
        while (Units--)
            sem_wait_timeout(Handle, Timeout);
    return AE_OK;
}

ACPI_STATUS AcpiOsSignalSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units)
{
    if (!Handle)
        return AE_BAD_PARAMETER;
    while (Units--)
        sem_post(Handle);
    return AE_OK;
}

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
    acpi_isr = Handler;
    acpi_isr_context = Context;
    install_isr(InterruptLevel, AcpiInt);
    return AE_OK;
}

ACPI_STATUS AcpiOsRemoveInterruptHandler(UINT32 InterruptLevel,
    ACPI_OSD_HANDLER Handler)
{
    uninstall_isr(InterruptLevel);
    return AE_OK;
}

// Port I/O
ACPI_STATUS AcpiOsReadPort(ACPI_IO_ADDRESS Address, UINT32 *Value, UINT32 Width)
{
    switch (Width)
    {
        case 8:
            *Value = inb(Address);
            break;
        case 16:
            *Value = inw(Address);
            break;
        case 32:
            *Value = inl(Address);
            break;
        default:
            return AE_BAD_PARAMETER;
    }
    return AE_OK;
}

ACPI_STATUS AcpiOsWritePort(ACPI_IO_ADDRESS Address, UINT32 Value, UINT32 Width)
{
    switch (Width)
    {
        case 8:
            outb(Address, Value);
            break;
        case 16:
            outw(Address, Value);
            break;
        case 32:
            outl(Address, Value);
            break;
        default:
            return AE_BAD_PARAMETER;
    }
    return AE_OK;
}

// Print
void AcpiOsPrintf(const char *Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    vprintf(Format, Args);
    va_end(Args);
}

void AcpiOsVprintf(const char *Format, va_list Args)
{
    vprintf(Format, Args);
}


ACPI_STATUS AcpiOsSignal(UINT32 Function, void *Info)
{
    return AE_OK;
}
