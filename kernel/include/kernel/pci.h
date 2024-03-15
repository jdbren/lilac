#ifndef _KERNEL_PCI_H
#define _KERNEL_PCI_H

#include <kernel/types.h>

u32 pciConfigRead(u8 bus, u8 slot, u8 func, u8 offset, u32 width);
void pciConfigWrite(u8 bus, u8 slot, u8 func, u8 offset, u32 data,
    u32 width);

u32 pciRead(u32 addr);
void pciWrite(u32 addr, u32 data);


struct pci_device_header {
    u16 vendor_id;
    u16 device_id;
    u16 command;
    u16 status;
    u8 revision_id;
    u8 prog_if;
    u8 subclass;
    u8 class;
    u8 cache_line_size;
};

#endif
