#ifndef _PCI_H
#define _PCI_H

#include <lilac/types.h>
#include <acpi/acpica.h>

#define PCI_TYPE0_ADDRESSES 6
#define PCI_TYPE1_ADDRESSES 2

int pcie_bus_init(ACPI_HANDLE PciBus);

u32 pciRead(u32 addr);
void pciWrite(u32 addr, u32 data);

u32 pciConfigRead(u8 bus, u8 slot, u8 func, u8 offset, u32 width);
void pciConfigWrite(u8 bus, u8 slot, u8 func, u8 offset, u32 data,
    u32 width);

void pcie_add_map(ACPI_TABLE_MCFG *mcfg);
void pci_read_device(ACPI_DEVICE_INFO *Info);


struct pci_device {
    u16 VendorID;
    u16 DeviceID;
    u16 Command;
    u16 Status;
    u8 RevisionID;
    u8 ProgIf;
    u8 SubClass;
    u8 BaseClass;
    u8 CacheLineSize;
    u8 LatencyTimer;
    u8 HeaderType;
    u8 BIST;

    union {
        struct _PCI_HEADER_TYPE_0 {
            u32 BaseAddresses[PCI_TYPE0_ADDRESSES];
            u32 CIS;
            u16 SubVendorID;
            u16 SubSystemID;
            u32 ROMBaseAddress;
            u8 CapabilitiesPtr;
            u8 Reserved1[7];

            u8 InterruptLine;
            u8 InterruptPin;
            u8 MinimumGrant;
            u8 MaximumLatency;
        } type0;

        struct _PCI_HEADER_TYPE_1 {
            u32 BaseAddresses[PCI_TYPE1_ADDRESSES];
            u8 PrimaryBusNumber;
            u8 SecondaryBusNumber;
            u8 SubordinateBusNumber;
            u8 SecondaryLatencyTimer;
            u8 IOBase;
            u8 IOLimit;
            u16 SecondaryStatus;
            u16 MemoryBase;
            u16 MemoryLimit;
            u16 PrefetchableMemoryBase;
            u16 PrefetchableMemoryLimit;
            u32 PrefetchableMemoryBaseUpper32;
            u32 PrefetchableMemoryLimitUpper32;
            u16 IOBaseUpper;
            u16 IOLimitUpper;
            u32 Reserved2;
            u32 ExpansionROMBase;
            u8 InterruptLine;
            u8 InterruptPin;
            u16 BridgeControl;
        } type1;

        struct _PCI_HEADER_TYPE_2 {
            u32 BaseAddress;
            u8 CapabilitiesPtr;
            u8 Reserved2;
            u16 SecondaryStatus;
            u8 PrimaryBusNumber;
            u8 CardbusBusNumber;
            u8 SubordinateBusNumber;
            u8 CardbusLatencyTimer;
            u32 MemoryBase0;
            u32 MemoryLimit0;
            u32 MemoryBase1;
            u32 MemoryLimit1;
            u16 IOBase0_LO;
            u16 IOBase0_HI;
            u16 IOLimit0_LO;
            u16 IOLimit0_HI;
            u16 IOBase1_LO;
            u16 IOBase1_HI;
            u16 IOLimit1_LO;
            u16 IOLimit1_HI;
            u8 InterruptLine;
            u8 InterruptPin;
            u16 BridgeControl;
            u16 SubVendorID;
            u16 SubSystemID;
            u32 LegacyBaseAddress;
            u8 Reserved3[56];
            u32 SystemControl;
            u8 MultiMediaControl;
            u8 GeneralStatus;
            u8 Reserved4[2];
            u8 GPIO0Control;
            u8 GPIO1Control;
            u8 GPIO2Control;
            u8 GPIO3Control;
            u32 IRQMuxRouting;
            u8 RetryStatus;
            u8 CardControl;
            u8 DeviceControl;
            u8 Diagnostic;
        } type2;

    } u;

    u8 DeviceSpecific[108];
};

#endif
