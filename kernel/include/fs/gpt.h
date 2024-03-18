#ifndef _GPT_H
#define _GPT_H

#include <kernel/types.h>

#define GPT_SIGNATURE 0x5452415020494645ULL
#define GPT_HEADER_LBA 1

struct GPT {
    u64 signature;
    u32 revision;
    u32 header_size;
    u32 header_crc32;
    u32 reserved;
    u64 my_lba;
    u64 alternate_lba;
    u64 first_usable_lba;
    u64 last_usable_lba;
    u64 disk_guid[2];
    u64 partition_entry_lba;
    u32 num_partition_entries;
    u32 size_of_partition_entry;
    u32 partition_entry_array_crc32;
} __attribute__((packed));

struct gpt_part_entry {
    u64 partition_type_guid[2];
    u64 unique_partition_guid[2];
    u64 starting_lba;
    u64 ending_lba;
    u64 attributes;
    u16 partition_name[36];
} __attribute__((packed));

#endif
