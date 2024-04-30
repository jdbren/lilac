#include <acpi/hpet.h>
#include <mm/kheap.h>


struct hpet_info *parse_hpet(struct SDTHeader *addr)
{
    struct hpet_info *info = kmalloc(sizeof(*info));
    struct hpet *hpet = (struct hpet*)addr;

    info->address = hpet->address.address;
    info->comparator_count = hpet->comparator_count;
    info->hpet_number = hpet->hpet_number;
    info->minimum_tick = hpet->minimum_tick;
    info->page_protection = hpet->page_protection;

    return info;
}

int dealloc_hpet(struct hpet_info *info)
{
    kfree(info);
    return 0;
}
