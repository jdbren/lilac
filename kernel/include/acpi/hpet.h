#ifndef _ACPI_HPET_H
#define _ACPI_HPET_H

#include <lilac/config.h>
#include <acpi/acpi.h>

struct __packed addr_struct {
    u8 address_space_id;    // 0 - system memory, 1 - system I/O
    u8 register_bit_width;
    u8 register_bit_offset;
    u8 reserved;
    u64 address;
};

struct __packed hpet {
    struct SDTHeader header;
    u8 hardware_rev_id;
    u8 comparator_count:5;
    u8 counter_size:1;
    u8 reserved:1;
    u8 legacy_replacement:1;
    u16 pci_vendor_id;
    struct addr_struct address;
    u8 hpet_number;
    u16 minimum_tick;
    u8 page_protection;
};

struct hpet_info {
    u64 address;
    u8 comparator_count;
    u8 hpet_number;
    u16 minimum_tick;
    u8 page_protection;
};

struct hpet_info *parse_hpet(struct SDTHeader *addr);
int dealloc_hpet(struct hpet_info *info);
void hpet_init(u32 time, struct hpet_info *info);
u64 hpet_read(void);
u32 hpet_get_clk_period(void);

#endif
