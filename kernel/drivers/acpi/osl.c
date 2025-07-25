// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)

/**
 * ACPICA OS Services Layer
 *
 * OS specific functions for ACPICA. This is not a full implementation, but
 * it is enough for basic functionality.
 *
*/

#include <lilac/types.h>
#include <lilac/config.h>
#include <lilac/boot.h>
#include <lilac/interrupt.h>
#include <lilac/port.h>
#include <lilac/process.h>
#include <lilac/timer.h>
#include <mm/kmm.h>
#include <mm/kmalloc.h>
#include <drivers/pci.h>
#include "acpica/acpi.h"

ACPI_OSD_HANDLER acpi_isr;
void *acpi_isr_context;

struct interrupt_frame;

ISR void AcpiInt(struct interrupt_frame *frame)
{
    printf("ACPI interrupt\n");
    (*acpi_isr)(acpi_isr_context);
}

ACPI_STATUS AcpiOsInitialize() { return AE_OK; }

ACPI_STATUS AcpiOsTerminate() { return AE_OK; }

ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer()
{
    return (ACPI_PHYSICAL_ADDRESS)get_rsdp();
}

ACPI_STATUS
AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES *PredefinedObject,
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

ACPI_STATUS AcpiOsPhysicalTableOverride(ACPI_TABLE_HEADER *ExistingTable,
    ACPI_PHYSICAL_ADDRESS *NewAddress, UINT32 *NewTableLength)
{
    *NewAddress = 0;
    *NewTableLength = 0;
    return AE_OK;
}


void *AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS PhysicalAddress, ACPI_SIZE Length)
{
    int offset = PhysicalAddress & 0xFFF;
    //printf("AcpiOsMapMemory: %x, %d\n", PhysicalAddress, Length);
    void *ptr = map_phys((void*)(uintptr_t)PhysicalAddress, offset + Length, PG_WRITE);
    return (void*)((uintptr_t)ptr + offset);
}

void AcpiOsUnmapMemory(void *where, ACPI_SIZE length)
{
    int offset = (uintptr_t)where & 0xFFF;
    unmap_phys(where, length + offset);
}

ACPI_STATUS AcpiOsGetPhysicalAddress(void *LogicalAddress,
    ACPI_PHYSICAL_ADDRESS *PhysicalAddress)
{
    *PhysicalAddress = (ACPI_PHYSICAL_ADDRESS)virt_to_phys(LogicalAddress);
    return AE_OK;
}

void *AcpiOsAllocate(ACPI_SIZE Size)
{
    void *ptr = kmalloc(Size);
    return ptr;
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
    return 1;
}

ACPI_STATUS AcpiOsExecute(ACPI_EXECUTE_TYPE Type, ACPI_OSD_EXEC_CALLBACK Function, void *Context)
{
    (*Function)(Context);
    return AE_OK;
}

void AcpiOsSleep(UINT64 Milliseconds)
{
    sleep(Milliseconds);
}

void AcpiOsStall(UINT32 Microseconds)
{
    sleep(Microseconds / 1000);
}

void AcpiOsWaitEventsComplete(void) {}

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
    *OutHandle = kmalloc(sizeof(spinlock_t));
    spin_lock_init(*OutHandle);
    return AE_OK;
}

void AcpiOsDeleteLock(ACPI_SPINLOCK Handle)
{
    if (!Handle)
        return;
    kfree(Handle);
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
    static int calls = 0;
    calls++;
    if (calls > 1)
        printf("WARNING: Multiple calls to AcpiOsInstallInterruptHandler\n");
    acpi_isr = Handler;
    acpi_isr_context = Context;
    install_isr(InterruptLevel, &AcpiInt);
    return AE_OK;
}

ACPI_STATUS AcpiOsRemoveInterruptHandler(UINT32 InterruptLevel,
    ACPI_OSD_HANDLER Handler)
{
    uninstall_isr(InterruptLevel);
    return AE_OK;
}

//Memory
ACPI_STATUS AcpiOsReadMemory(ACPI_PHYSICAL_ADDRESS Address, UINT64 *Value,
    UINT32 Width)
{
    void *addr = map_phys((void*)(uintptr_t)(Address), PAGE_SIZE, PG_WRITE);
    switch (Width)
    {
        case 8:
            *Value = *(u8*)addr;
            break;
        case 16:
            *Value = *(u16*)addr;
            break;
        case 32:
            *Value = *(u32*)addr;
            break;
        case 64:
            *Value = *(u64*)addr;
            break;
        default:
            return AE_BAD_PARAMETER;
    }
    unmap_phys(addr, PAGE_SIZE);
    return AE_OK;
}

ACPI_STATUS AcpiOsWriteMemory(ACPI_PHYSICAL_ADDRESS Address, UINT64 Value,
    UINT32 Width)
{
    void *addr = map_phys((void*)(uintptr_t)(Address), PAGE_SIZE, PG_WRITE);
    switch (Width)
    {
        case 8:
            *(u8*)addr = Value;
            break;
        case 16:
            *(u16*)addr = Value;
            break;
        case 32:
            *(u32*)addr = Value;
            break;
        case 64:
            *(u64*)addr = Value;
            break;
        default:
            return AE_BAD_PARAMETER;
    }
    unmap_phys(addr, PAGE_SIZE);
    return AE_OK;
}


// Port I/O
ACPI_STATUS AcpiOsReadPort(ACPI_IO_ADDRESS Address, UINT32 *Value, UINT32 Width)
{
    if (Value == NULL || (Width != 8 && Width != 16 && Width != 32))
        return AE_BAD_PARAMETER;
    *Value = read_port(Address, Width);
    return AE_OK;
}

ACPI_STATUS AcpiOsWritePort(ACPI_IO_ADDRESS Address, UINT32 Value, UINT32 Width)
{
    if (Width != 8 && Width != 16 && Width != 32)
        return AE_BAD_PARAMETER;
    write_port(Address, Value, Width);
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


// PCI
ACPI_STATUS AcpiOsReadPciConfiguration(ACPI_PCI_ID *PciId, UINT32 Register,
    UINT64 *Value, UINT32 Width)
{
    *Value = (u64)pciConfigRead(PciId->Bus, PciId->Device, PciId->Function, (u8)Register, Width);
    return AE_OK;
}

ACPI_STATUS AcpiOsWritePciConfiguration(ACPI_PCI_ID *PciId, UINT32 Register,
    UINT64 Value, UINT32 Width)
{
    pciConfigWrite(PciId->Bus, PciId->Device, PciId->Function, (u8)Register, Value, Width);
    return AE_OK;
}


// Other
UINT64 AcpiOsGetTimer(void)
{
    return get_sys_time();
}

ACPI_STATUS AcpiOsSignal(UINT32 Function, void *Info)
{
    return AE_OK;
}

ACPI_STATUS AcpiOsEnterSleep(UINT8 SleepState, UINT32 RegisterAValue,
    UINT32 RegisterBValue)
{
    return AE_OK;
}
