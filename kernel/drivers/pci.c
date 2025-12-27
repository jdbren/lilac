#include <limits.h>
#include <drivers/pci.h>
#include <lilac/libc.h>
#include <lilac/log.h>
#include <lilac/device.h>
#include <lilac/port.h>
#include <lilac/panic.h>
#include <mm/kmm.h>
#include <mm/kmalloc.h>
#include <mm/page.h>
#include <drivers/ahci.h>

#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static struct pcie_mmio_map {
    uintptr_t base;
    void *virt_base;
    u8 start_bus;
    u8 end_bus;
} *pcie_mmio_maps;
static int pcie_mmio_map_cnt;

static void * get_pci_mmio_addr(int bus, int device, int function);
void pcie_read_device(ACPI_DEVICE_INFO *Info, int bus);
static ACPI_STATUS pcie_init_device(ACPI_HANDLE ObjHandle, UINT32 Level,
    void *Context, void **ReturnValue);

int pcie_bus_init(ACPI_HANDLE PciBus)
{
    ACPI_TABLE_MCFG *mcfg;
    ACPI_STATUS status = AcpiGetTable("MCFG", 0, (ACPI_TABLE_HEADER**)&mcfg);

    if (ACPI_SUCCESS(status))
        pcie_add_map(mcfg);
    else
        kerror("No MCFG table found\n");

    size_t bus_num = 0;
    ACPI_OBJECT bbn_obj;
    ACPI_BUFFER bbn_buffer = { sizeof(bbn_obj), &bbn_obj };
    status = AcpiEvaluateObject(PciBus, "_BBN", NULL, &bbn_buffer);
    if (ACPI_SUCCESS(status) && bbn_obj.Type == ACPI_TYPE_INTEGER) {
        bus_num = bbn_obj.Integer.Value;
    }

    klog(LOG_DEBUG, "Reading PCIe devices on bus %u at depth 1\n", (unsigned) bus_num);
    status = AcpiWalkNamespace(ACPI_TYPE_DEVICE, PciBus, 1,
 		pcie_init_device, NULL, (void *)bus_num, NULL);

    if (ACPI_FAILURE(status))
        return -1;
    return 0;
}

static ACPI_STATUS pcie_init_device(ACPI_HANDLE ObjHandle, UINT32 Level,
    void *Context, void **ReturnValue)
{
    ACPI_STATUS Status;
    ACPI_BUFFER Path;
    char Buffer[256];
    Path.Length = sizeof(Buffer);
    Path.Pointer = Buffer;
    ACPI_DEVICE_INFO *Info = kzmalloc(sizeof(*Info));
    int bus = (int)(size_t)Context;

    Status = AcpiGetName(ObjHandle, ACPI_FULL_PATHNAME, &Path);
    if (ACPI_SUCCESS(Status)) {
#ifdef DEBUG_PCI
        klog(LOG_DEBUG, "\tFound %s on bus %d\n", Path.Pointer, bus);
#endif
    } else {
        return Status;
    }

    Status = AcpiGetObjectInfo(ObjHandle, &Info);
    if (ACPI_SUCCESS(Status)) {
        if (Info->Valid & ACPI_VALID_ADR)
            pcie_read_device(Info, bus);
    }

    kfree(Info);
    return AE_OK;
}

void pcie_read_device(ACPI_DEVICE_INFO *Info, int bus)
{
    struct pci_device *pci_dev;
    u8 dev, fn;

    if (!(Info->Valid & ACPI_VALID_ADR))
        return;

    dev = (Info->Address >> 16) & 0xFF;
    fn = Info->Address & 0x7;

    pci_dev = (struct pci_device *) get_pci_mmio_addr(bus, dev, fn);
    if (!pci_dev || pci_dev->VendorID == 0xFFFF) {
        return;
    }

    if ((pci_dev->HeaderType & 0x7F) == 0x00 &&
            pci_dev->BaseClass == 0x01 &&
            pci_dev->SubClass == 0x06 &&
            pci_dev->ProgIf == 0x01) {
        klog(LOG_INFO, "Found AHCI Controller at %02x:%02x.%x\n", bus, dev, fn);
        ahci_init((void *)(uintptr_t)(pci_dev->u.type0.BaseAddresses[5] & 0xFFFFF000));
    }
}

/*
void pci_read_device(ACPI_DEVICE_INFO *Info)
{
    ACPI_STATUS Status;

    struct pci_device *pci_dev = kzmalloc(sizeof(*pci_dev));
    if (!pci_dev) {
        kerror("Failed to allocate memory for PCI device\n");
    }
    u32 *pci_reg = (u32*)pci_dev;

    if (Info->Valid & ACPI_VALID_ADR) {
        pci_reg = (u32*)pci_dev;
        *pci_reg = pciConfigRead(0, Info->Address >> 16, Info->Address & 0xFFFF, 0, 32);
        if (*pci_reg++ != 0xffffffff) {
            *pci_reg++ = pciConfigRead(0, Info->Address >> 16, Info->Address & 0xFFFF, 4, 32);
            *pci_reg++ = pciConfigRead(0, Info->Address >> 16, Info->Address & 0xFFFF, 8, 32);
            *pci_reg = pciConfigRead(0, Info->Address >> 16, Info->Address & 0xFFFF, 12, 32);
        }
    }
}
*/


/**
 * Memory-mapped I/O for PCIe devices
*/

// Get the MMIO address for a PCI device
static void * get_pci_mmio_addr(int bus, int device, int function)
{
    for (int i = 0; i < pcie_mmio_map_cnt; i++) {
        struct pcie_mmio_map *m = &pcie_mmio_maps[i];
        if (bus < m->start_bus || bus > m->end_bus)
            continue;

        u32 offset = ((bus - m->start_bus) << 20) | (device << 15) | (function << 12);

        return (void*)((uintptr_t)m->virt_base + offset);
    }

    return NULL;
}

// Add a PCI MMIO map from the ACPI MCFG table
void pcie_add_map(ACPI_TABLE_MCFG *mcfg)
{
    ACPI_MCFG_ALLOCATION *mcfg_alloc;

    pcie_mmio_map_cnt = 0;
    pcie_mmio_maps = kcalloc(4, sizeof(*pcie_mmio_maps));
    size_t cap = 4;

    klog(LOG_INFO, "Parsing MCFG table for PCIe MMIO maps...\n");

    if (!pcie_mmio_maps) {
        kerror("Failed to allocate memory for PCIe MMIO maps\n");
    }

    mcfg_alloc = (ACPI_MCFG_ALLOCATION*)((uintptr_t)mcfg + sizeof(*mcfg));
    while ((uintptr_t)mcfg_alloc < (uintptr_t)mcfg + mcfg->Header.Length)
    {
        if (pcie_mmio_map_cnt == cap) {
            cap *= 2;
            pcie_mmio_maps = krealloc(pcie_mmio_maps, cap * sizeof(*pcie_mmio_maps));
            if (!pcie_mmio_maps) {
                kerror("Failed to reallocate memory for PCIe MMIO maps\n");
            }
        }

        uint8_t start = mcfg_alloc->StartBusNumber;
        uint8_t end = mcfg_alloc->EndBusNumber;
        size_t size = (end - start + 1) << 20;

#ifdef __x86_64__
        void *va = get_free_vaddr(PAGE_UP_COUNT(size));
#else
        void *va = (void*)0x90000000;
#endif

        map_pages((void *)(uintptr_t)mcfg_alloc->Address, va,
            MEM_PF_WRITE | MEM_PF_UC | MEM_PF_NO_EXEC, PAGE_UP_COUNT(size));

        pcie_mmio_maps[pcie_mmio_map_cnt++] =
            (struct pcie_mmio_map) {
                .base = mcfg_alloc->Address,
                .virt_base = va,
                .start_bus = start,
                .end_bus = end,
            };

        mcfg_alloc++;
    }

    klog(LOG_INFO, "Added %d PCIe MMIO maps\n", pcie_mmio_map_cnt);
}


/**
 * PCI configuration space access
*/

u32 pciRead(u32 addr)
{
    write_port(PCI_CONFIG_ADDRESS, addr | (1U << 31), 32);
    return read_port(PCI_CONFIG_DATA, 32);
}

void pciWrite(u32 addr, u32 data)
{
    write_port(PCI_CONFIG_ADDRESS, addr | (1U << 31), 32);
    write_port(PCI_CONFIG_DATA, data, 32);
}

u32 pciConfigRead(u8 bus, u8 slot, u8 func, u8 offset, u32 width)
{
    if (width == 64) {
        printf("64-bit PCI configuration not supported\n");
        return 0;
    }

    u32 address;
    u32 lbus  = (u32)bus;
    u32 lslot = (u32)slot;
    u32 lfunc = (u32)func;
    u32 tmp = 0;

    // Create configuration address
    address = (u32)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xFC) | (1U << 31));

    // Write out the address
    write_port(PCI_CONFIG_ADDRESS, address, 32);
    // Read in the data
    tmp = read_port(PCI_CONFIG_DATA, 32);
    switch (width) {
        case 8:
            tmp >>= ((offset & 3) * 8) & 0xFF;
            break;
        case 16:
            tmp >>= ((offset & 2) * 8) & 0xFFFF;
            break;
        case 32:
            break;
    }
    return tmp;
}

void pciConfigWrite(u8 bus, u8 slot, u8 func, u8 offset, u32 data, u32 width)
{
    if (width == 64) {
        printf("64-bit PCI configuration not supported\n");
        return;
    }

    u32 address;
    u32 lbus  = (u32)bus;
    u32 lslot = (u32)slot;
    u32 lfunc = (u32)func;

    // Create configuration address as per Figure 1
    address = (u32)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xFC) | (1U << 31));

    // Write out the address
    write_port(PCI_CONFIG_ADDRESS, address, 32);
    write_port(PCI_CONFIG_DATA, (u8)data, width);
}
