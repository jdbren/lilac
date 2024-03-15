#include <kernel/pci.h>
#include <kernel/port.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

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
