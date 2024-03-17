#include <drivers/pci.h>
#include <kernel/port.h>
#include <mm/kmm.h>
#include <mm/kheap.h>
#include <drivers/ahci.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static struct {
    uintptr_t base;
    u8 start_bus;
    u8 end_bus;
} *pcie_mmio_maps;
static int pcie_mmio_map_cnt;



uintptr_t get_pci_mmio_addr(u8 bus, u8 device, u8 function)
{
    uintptr_t addr;
    for (int i = 0; i < 256; i++) {
        if (pcie_mmio_maps[i].base == 0)
            break;
        if (bus >= pcie_mmio_maps[i].start_bus && bus <= pcie_mmio_maps[i].end_bus) {
            addr = pcie_mmio_maps[i].base;
            addr += ((bus - pcie_mmio_maps[i].start_bus) << 20 | device << 15 | function << 12);

            map_to_self((void*)addr, 1024, PG_WRITE | PG_CACHE_DISABLE);
            return addr;
        }
    }

    return 0;
}

void pcie_add_map(ACPI_TABLE_MCFG *mcfg)
{
    ACPI_MCFG_ALLOCATION *mcfg_alloc;

    pcie_mmio_map_cnt = 4;
    pcie_mmio_maps = kcalloc(pcie_mmio_map_cnt, sizeof(*pcie_mmio_maps));

    mcfg_alloc = (ACPI_MCFG_ALLOCATION*)((u32)mcfg + sizeof(*mcfg));

    printf("MCFG:\n");
    printf(" Size: %x\n", mcfg->Header.Length);
    for (int i = 0;
        (uintptr_t)mcfg_alloc < (uintptr_t)mcfg + mcfg->Header.Length;
        mcfg_alloc++, i++)
    {
        if (i >= pcie_mmio_map_cnt) {
            pcie_mmio_map_cnt *= 2;
            pcie_mmio_maps = krealloc(pcie_mmio_maps,
                                pcie_mmio_map_cnt * sizeof(*pcie_mmio_maps));
        }
        pcie_mmio_maps[i].base = mcfg_alloc->Address;
        pcie_mmio_maps[i].start_bus = mcfg_alloc->StartBusNumber;
        pcie_mmio_maps[i].end_bus = mcfg_alloc->EndBusNumber;

        printf(" Base Address: %x\n", mcfg_alloc->Address);
        printf(" PCI Segment Group: %x\n", mcfg_alloc->PciSegment);
        printf(" Start Bus Number: %x\n", mcfg_alloc->StartBusNumber);
        printf(" End Bus Number: %x\n", mcfg_alloc->EndBusNumber);
    }
}

void pcie_read_device(ACPI_DEVICE_INFO *Info)
{
    ACPI_STATUS Status;
    ACPI_BUFFER Path;
    char Buffer[256];
    Path.Length = sizeof(Buffer);
    Path.Pointer = Buffer;
    struct pci_device *pci_dev;

    if (Info->Valid & ACPI_VALID_ADR) {
        pci_dev = (struct pci_device*)get_pci_mmio_addr(0, Info->Address >> 16,
                                                        Info->Address & 0xFFFF);
        printf(" PCIe Device: %x:%x\n", pci_dev->VendorID, pci_dev->DeviceID);
        printf("  PCI Class: %x:%x\n", pci_dev->BaseClass, pci_dev->SubClass);
        printf("  Prog IF: %x\n", pci_dev->ProgIf);
        printf("  Command: %x\n", pci_dev->Command);
        printf("  Status: %x\n", pci_dev->Status);
        printf("  Cache Line Size: %x\n", pci_dev->CacheLineSize);
        printf("  Latency Timer: %x\n", pci_dev->LatencyTimer);
        printf("  Header Type: %x\n", pci_dev->HeaderType);
        printf("  BIST: %x\n", pci_dev->BIST);

        if ((pci_dev->HeaderType & 0xf) == 0) {
            printf("  BAR0: %x\n", pci_dev->u.type0.BaseAddresses[0]);
            printf("  BAR1: %x\n", pci_dev->u.type0.BaseAddresses[1]);
            printf("  BAR2: %x\n", pci_dev->u.type0.BaseAddresses[2]);
            printf("  BAR3: %x\n", pci_dev->u.type0.BaseAddresses[3]);
            printf("  BAR4: %x\n", pci_dev->u.type0.BaseAddresses[4]);
            printf("  BAR5: %x\n", pci_dev->u.type0.BaseAddresses[5]);
            printf("  CIS: %x\n", pci_dev->u.type0.CIS);
            printf("  SubVendorID: %x\n", pci_dev->u.type0.SubVendorID);
            printf("  SubSystemID: %x\n", pci_dev->u.type0.SubSystemID);
            printf("  ROM Base Address: %x\n", pci_dev->u.type0.ROMBaseAddress);
            printf("  Interrupt Line: %x\n", pci_dev->u.type0.InterruptLine);
            printf("  Interrupt Pin: %x\n", pci_dev->u.type0.InterruptPin);
            printf("  Minimum Grant: %x\n", pci_dev->u.type0.MinimumGrant);
            printf("  Maximum Latency: %x\n", pci_dev->u.type0.MaximumLatency);
        }

        if (pci_dev->BaseClass == 1 && pci_dev->SubClass == 6) {
            printf("AHCI Controller:\n");
            ahci_init((void*)(pci_dev->u.type0.BaseAddresses[5] & 0xFFFFF000));
        }
    }
}

void pci_read_device(ACPI_DEVICE_INFO *Info)
{
    ACPI_STATUS Status;
    ACPI_BUFFER Path;
    char Buffer[256];
    Path.Length = sizeof(Buffer);
    Path.Pointer = Buffer;

    struct pci_device *pci_dev = kzmalloc(sizeof(*pci_dev));
    u32 *pci_reg = (u32*)pci_dev;

    if (Info->Valid & ACPI_VALID_ADR) {
        pci_reg = (u32*)pci_dev;
        *pci_reg = pciConfigRead(0, Info->Address >> 16, Info->Address & 0xFFFF, 0, 32);
        if (*pci_reg++ != 0xffffffff) {
            *pci_reg++ = pciConfigRead(0, Info->Address >> 16, Info->Address & 0xFFFF, 4, 32);
            *pci_reg++ = pciConfigRead(0, Info->Address >> 16, Info->Address & 0xFFFF, 8, 32);
            *pci_reg = pciConfigRead(0, Info->Address >> 16, Info->Address & 0xFFFF, 12, 32);

            printf(" PCI Device: %x:%x\n", pci_dev->VendorID, pci_dev->DeviceID);
            printf("  PCI Class: %x:%x\n", pci_dev->BaseClass, pci_dev->SubClass);
            printf("  Prog IF: %x\n", pci_dev->ProgIf);
            printf("  Command: %x\n", pci_dev->Command);
            printf("  Status: %x\n", pci_dev->Status);
            printf("  Cache Line Size: %x\n", pci_dev->CacheLineSize);
            printf("  Latency Timer: %x\n", pci_dev->LatencyTimer);
            printf("  Header Type: %x\n", pci_dev->HeaderType);
            printf("  BIST: %x\n", pci_dev->BIST);
        }
    }
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
    WritePort(PCI_CONFIG_ADDRESS, address, 32);
    // Read in the data
    tmp = ReadPort(PCI_CONFIG_DATA, 32);
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
    WritePort(PCI_CONFIG_ADDRESS, address, 32);
    WritePort(PCI_CONFIG_DATA, (u8)data, width);
}

u32 pciRead(u32 addr)
{
    WritePort(PCI_CONFIG_ADDRESS, addr | (1U << 31), 32);
    return ReadPort(PCI_CONFIG_DATA, 32);
}

void pciWrite(u32 addr, u32 data)
{
    WritePort(PCI_CONFIG_ADDRESS, addr | (1U << 31), 32);
    WritePort(PCI_CONFIG_DATA, data, 32);
}