#ifndef _KERNEL_EFI_H
#define _KERNEL_EFI_H

#include <lilac/types.h>

#define UUID_SIZE 16

typedef struct {
	u8 b[UUID_SIZE];
} guid_t;

#define EFI_SUCCESS		        0
#define EFI_LOAD_ERROR		    ( 1 | (1UL << (BITS_PER_LONG-1)))
#define EFI_INVALID_PARAMETER	( 2 | (1UL << (BITS_PER_LONG-1)))
#define EFI_UNSUPPORTED		    ( 3 | (1UL << (BITS_PER_LONG-1)))
#define EFI_BAD_BUFFER_SIZE	    ( 4 | (1UL << (BITS_PER_LONG-1)))
#define EFI_BUFFER_TOO_SMALL	( 5 | (1UL << (BITS_PER_LONG-1)))
#define EFI_NOT_READY		    ( 6 | (1UL << (BITS_PER_LONG-1)))
#define EFI_DEVICE_ERROR	    ( 7 | (1UL << (BITS_PER_LONG-1)))
#define EFI_WRITE_PROTECTED	    ( 8 | (1UL << (BITS_PER_LONG-1)))
#define EFI_OUT_OF_RESOURCES	( 9 | (1UL << (BITS_PER_LONG-1)))
#define EFI_NOT_FOUND		    (14 | (1UL << (BITS_PER_LONG-1)))
#define EFI_ACCESS_DENIED	    (15 | (1UL << (BITS_PER_LONG-1)))
#define EFI_TIMEOUT		        (18 | (1UL << (BITS_PER_LONG-1)))
#define EFI_ABORTED		        (21 | (1UL << (BITS_PER_LONG-1)))
#define EFI_SECURITY_VIOLATION	(26 | (1UL << (BITS_PER_LONG-1)))

typedef unsigned long efi_status_t;
typedef u8 efi_bool_t;
typedef u16 efi_char16_t;		/* UNICODE character */
typedef u64 efi_physical_addr_t;
typedef void *efi_handle_t;

#define guid_t(a, b, c, d...) (guid_t){ {					            \
	(a) & 0xff, ((a) >> 8) & 0xff, ((a) >> 16) & 0xff, ((a) >> 24) & 0xff,	\
	(b) & 0xff, ((b) >> 8) & 0xff,						                    \
	(c) & 0xff, ((c) >> 8) & 0xff, d } }

/*
 * Generic EFI table header
 */
typedef	struct {
	u64 signature;
	u32 revision;
	u32 headersize;
	u32 crc32;
	u32 reserved;
} efi_table_hdr_t;

/*
 * Memory map descriptor:
 */

/* Memory types: */
#define EFI_RESERVED_TYPE		        0
#define EFI_LOADER_CODE			        1
#define EFI_LOADER_DATA			        2
#define EFI_BOOT_SERVICES_CODE		    3
#define EFI_BOOT_SERVICES_DATA		    4
#define EFI_RUNTIME_SERVICES_CODE	    5
#define EFI_RUNTIME_SERVICES_DATA	    6
#define EFI_CONVENTIONAL_MEMORY		    7
#define EFI_UNUSABLE_MEMORY		        8
#define EFI_ACPI_RECLAIM_MEMORY		    9
#define EFI_ACPI_MEMORY_NVS		        10
#define EFI_MEMORY_MAPPED_IO		    11
#define EFI_MEMORY_MAPPED_IO_PORT_SPACE	12
#define EFI_PAL_CODE			        13
#define EFI_PERSISTENT_MEMORY		    14
#define EFI_UNACCEPTED_MEMORY		    15
#define EFI_MAX_MEMORY_TYPE		        16

/* Attribute values: */
#define EFI_MEMORY_UC		((u64)0x0000000000000001ULL)	/* uncached */
#define EFI_MEMORY_WC		((u64)0x0000000000000002ULL)	/* write-coalescing */
#define EFI_MEMORY_WT		((u64)0x0000000000000004ULL)	/* write-through */
#define EFI_MEMORY_WB		((u64)0x0000000000000008ULL)	/* write-back */
#define EFI_MEMORY_UCE		((u64)0x0000000000000010ULL)	/* uncached, exported */
#define EFI_MEMORY_WP		((u64)0x0000000000001000ULL)	/* write-protect */
#define EFI_MEMORY_RP		((u64)0x0000000000002000ULL)	/* read-protect */
#define EFI_MEMORY_XP		((u64)0x0000000000004000ULL)	/* execute-protect */
#define EFI_MEMORY_NV		((u64)0x0000000000008000ULL)	/* non-volatile */
#define EFI_MEMORY_MORE_RELIABLE \
				((u64)0x0000000000010000ULL)	/* higher reliability */
#define EFI_MEMORY_RO		((u64)0x0000000000020000ULL)	/* read-only */
#define EFI_MEMORY_SP		((u64)0x0000000000040000ULL)	/* soft reserved */
#define EFI_MEMORY_CPU_CRYPTO	((u64)0x0000000000080000ULL)	/* supports encryption */
#define EFI_MEMORY_RUNTIME	((u64)0x8000000000000000ULL)	/* range requires runtime mapping */
#define EFI_MEMORY_DESCRIPTOR_VERSION	1

#define EFI_PAGE_SHIFT		12
#define EFI_PAGE_SIZE		(1UL << EFI_PAGE_SHIFT)
#define EFI_PAGES_MAX		(U64_MAX >> EFI_PAGE_SHIFT)

typedef struct {
	u32 type;
	u32 pad;
	u64 phys_addr;
	u64 virt_addr;
	u64 num_pages;
	u64 attribute;
} efi_memory_desc_t;

typedef struct {
	guid_t guid;
	u32 headersize;
	u32 flags;
	u32 imagesize;
} efi_capsule_header_t;


#define NULL_GUID				                guid_t(0x00000000, 0x0000, 0x0000,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00)
#define ACPI_TABLE_GUID				            guid_t(0xeb9d2d30, 0x2d88, 0x11d3,  0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d)
#define ACPI_20_TABLE_GUID			            guid_t(0x8868e871, 0xe4f1, 0x11d3,  0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81)
#define SMBIOS_TABLE_GUID			            guid_t(0xeb9d2d31, 0x2d88, 0x11d3,  0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d)
#define SMBIOS3_TABLE_GUID			            guid_t(0xf2fd1544, 0x9794, 0x4a2c,  0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94)
#define UGA_IO_PROTOCOL_GUID			        guid_t(0x61a4d49e, 0x6f68, 0x4f1b,  0xb9, 0x22, 0xa8, 0x6e, 0xed, 0x0b, 0x07, 0xa2)
#define EFI_GLOBAL_VARIABLE_GUID		        guid_t(0x8be4df61, 0x93ca, 0x11d2,  0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c)
#define UV_SYSTEM_TABLE_GUID			        guid_t(0x3b13a7d4, 0x633e, 0x11dd,  0x93, 0xec, 0xda, 0x25, 0x56, 0xd8, 0x95, 0x93)
#define LINUX_EFI_CRASH_GUID			        guid_t(0xcfc8fc79, 0xbe2e, 0x4ddc,  0x97, 0xf0, 0x9f, 0x98, 0xbf, 0xe2, 0x98, 0xa0)
#define LOADED_IMAGE_PROTOCOL_GUID		        guid_t(0x5b1b31a1, 0x9562, 0x11d2,  0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)
#define LOADED_IMAGE_DEVICE_PATH_PROTOCOL_GUID	guid_t(0xbc62157e, 0x3e33, 0x4fec,  0x99, 0x20, 0x2d, 0x3b, 0x36, 0xd7, 0x50, 0xdf)
#define EFI_DEVICE_PATH_PROTOCOL_GUID		    guid_t(0x09576e91, 0x6d3f, 0x11d2,  0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)
#define EFI_DEVICE_PATH_TO_TEXT_PROTOCOL_GUID	guid_t(0x8b843e20, 0x8132, 0x4852,  0x90, 0xcc, 0x55, 0x1a, 0x4e, 0x4a, 0x7f, 0x1c)
#define EFI_DEVICE_PATH_FROM_TEXT_PROTOCOL_GUID	guid_t(0x05c99a21, 0xc70f, 0x4ad2,  0x8a, 0x5f, 0x35, 0xdf, 0x33, 0x43, 0xf5, 0x1e)
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID	    guid_t(0x9042a9de, 0x23dc, 0x4a38,  0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a)
#define EFI_UGA_PROTOCOL_GUID			        guid_t(0x982c298b, 0xf4fa, 0x41cb,  0xb8, 0x38, 0x77, 0xaa, 0x68, 0x8f, 0xb8, 0x39)
#define EFI_PCI_IO_PROTOCOL_GUID		        guid_t(0x4cf5b200, 0x68b8, 0x4ca5,  0x9e, 0xec, 0xb2, 0x3e, 0x3f, 0x50, 0x02, 0x9a)
#define EFI_FILE_INFO_ID			            guid_t(0x09576e92, 0x6d3f, 0x11d2,  0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)
#define EFI_SYSTEM_RESOURCE_TABLE_GUID		    guid_t(0xb122a263, 0x3661, 0x4f68,  0x99, 0x29, 0x78, 0xf8, 0xb0, 0xd6, 0x21, 0x80)
#define EFI_FILE_SYSTEM_GUID			        guid_t(0x964e5b22, 0x6459, 0x11d2,  0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)
#define DEVICE_TREE_GUID			            guid_t(0xb1b621d5, 0xf19c, 0x41a5,  0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0)
#define EFI_PROPERTIES_TABLE_GUID		        guid_t(0x880aaca3, 0x4adc, 0x4a04,  0x90, 0x79, 0xb7, 0x47, 0x34, 0x08, 0x25, 0xe5)
#define EFI_RNG_PROTOCOL_GUID			        guid_t(0x3152bca5, 0xeade, 0x433d,  0x86, 0x2e, 0xc0, 0x1c, 0xdc, 0x29, 0x1f, 0x44)
#define EFI_RNG_ALGORITHM_RAW			        guid_t(0xe43176d7, 0xb6e8, 0x4827,  0xb7, 0x84, 0x7f, 0xfd, 0xc4, 0xb6, 0x85, 0x61)
#define EFI_MEMORY_ATTRIBUTES_TABLE_GUID	    guid_t(0xdcfa911d, 0x26eb, 0x469f,  0xa2, 0x20, 0x38, 0xb7, 0xdc, 0x46, 0x12, 0x20)
#define EFI_CONSOLE_OUT_DEVICE_GUID		        guid_t(0xd3b36f2c, 0xd551, 0x11d4,  0x9a, 0x46, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d)
#define APPLE_PROPERTIES_PROTOCOL_GUID		    guid_t(0x91bd12fe, 0xf6c3, 0x44fb,  0xa5, 0xb7, 0x51, 0x22, 0xab, 0x30, 0x3a, 0xe0)
#define EFI_TCG2_PROTOCOL_GUID			        guid_t(0x607f766c, 0x7455, 0x42be,  0x93, 0x0b, 0xe4, 0xd7, 0x6d, 0xb2, 0x72, 0x0f)
#define EFI_LOAD_FILE_PROTOCOL_GUID		        guid_t(0x56ec3091, 0x954c, 0x11d2,  0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)
#define EFI_LOAD_FILE2_PROTOCOL_GUID		    guid_t(0x4006c0c1, 0xfcb3, 0x403e,  0x99, 0x6d, 0x4a, 0x6c, 0x87, 0x24, 0xe0, 0x6d)
#define EFI_RT_PROPERTIES_TABLE_GUID		    guid_t(0xeb66918a, 0x7eef, 0x402a,  0x84, 0x2e, 0x93, 0x1d, 0x21, 0xc3, 0x8a, 0xe9)
#define EFI_DXE_SERVICES_TABLE_GUID		        guid_t(0x05ad34ba, 0x6f02, 0x4214,  0x95, 0x2e, 0x4d, 0xa0, 0x39, 0x8e, 0x2b, 0xb9)
#define EFI_SMBIOS_PROTOCOL_GUID		        guid_t(0x03583ff6, 0xcb36, 0x4940,  0x94, 0x7e, 0xb9, 0xb3, 0x9f, 0x4a, 0xfa, 0xf7)
#define EFI_MEMORY_ATTRIBUTE_PROTOCOL_GUID	    guid_t(0xf4560cf6, 0x40ec, 0x4b4a,  0xa1, 0x92, 0xbf, 0x1d, 0x57, 0xd0, 0xb1, 0x89)

#define EFI_IMAGE_SECURITY_DATABASE_GUID	guid_t(0xd719b2cb, 0x3d3a, 0x4596,  0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f)
#define EFI_SHIM_LOCK_GUID			        guid_t(0x605dab50, 0xe046, 0x4300,  0xab, 0xb6, 0x3d, 0xd8, 0x10, 0xdd, 0x8b, 0x23)

#define EFI_CERT_SHA256_GUID		guid_t(0xc1c41626, 0x504c, 0x4092, 0xac, 0xa9, 0x41, 0xf9, 0x36, 0x93, 0x43, 0x28)
#define EFI_CERT_X509_GUID			guid_t(0xa5c059a1, 0x94e4, 0x4aa7, 0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72)
#define EFI_CERT_X509_SHA256_GUID	guid_t(0x3bd2a492, 0x96c0, 0x4079, 0xb4, 0x20, 0xfc, 0xf9, 0x8e, 0xf1, 0x03, 0xed)
#define EFI_CC_BLOB_GUID			guid_t(0x067b1f5f, 0xcf26, 0x44c5, 0x85, 0x54, 0x93, 0xd7, 0x77, 0x91, 0x2d, 0x42)


typedef struct {
	guid_t guid;
	u64 table;
} efi_config_table_64_t;

typedef struct {
	guid_t guid;
	u32 table;
} efi_config_table_32_t;

typedef union {
	struct {
		guid_t guid;
		void *table;
	};
	efi_config_table_32_t mixed_mode;
} efi_config_table_t;

typedef struct {
	guid_t guid;
	unsigned long *ptr;
	const char name[16];
} efi_config_table_type_t;

#define EFI_SYSTEM_TABLE_SIGNATURE ((u64)0x5453595320494249ULL)
#define EFI_DXE_SERVICES_TABLE_SIGNATURE ((u64)0x565245535f455844ULL)

#define EFI_2_30_SYSTEM_TABLE_REVISION  ((2 << 16) | (30))
#define EFI_2_20_SYSTEM_TABLE_REVISION  ((2 << 16) | (20))
#define EFI_2_10_SYSTEM_TABLE_REVISION  ((2 << 16) | (10))
#define EFI_2_00_SYSTEM_TABLE_REVISION  ((2 << 16) | (00))
#define EFI_1_10_SYSTEM_TABLE_REVISION  ((1 << 16) | (10))
#define EFI_1_02_SYSTEM_TABLE_REVISION  ((1 << 16) | (02))


typedef struct {
	efi_table_hdr_t hdr;
	u64 fw_vendor;	/* physical addr of CHAR16 vendor string */
	u32 fw_revision;
	u32 __pad1;
	u64 con_in_handle;
	u64 con_in;
	u64 con_out_handle;
	u64 con_out;
	u64 stderr_handle;
	u64 stderr;
	u64 runtime;
	u64 boottime;
	u32 nr_tables;
	u32 __pad2;
	u64 tables;
} efi_system_table_64_t;

typedef struct {
	efi_table_hdr_t hdr;
	u32 fw_vendor;	/* physical addr of CHAR16 vendor string */
	u32 fw_revision;
	u32 con_in_handle;
	u32 con_in;
	u32 con_out_handle;
	u32 con_out;
	u32 stderr_handle;
	u32 stderr;
	u32 runtime;
	u32 boottime;
	u32 nr_tables;
	u32 tables;
} efi_system_table_32_t;

typedef union efi_simple_text_input_protocol efi_simple_text_input_protocol_t;
typedef union efi_simple_text_output_protocol efi_simple_text_output_protocol_t;

typedef union {
	struct {
		efi_table_hdr_t hdr;
		unsigned long fw_vendor;	/* physical addr of CHAR16 vendor string */
		u32 fw_revision;
		unsigned long con_in_handle;
		efi_simple_text_input_protocol_t *con_in;
		unsigned long con_out_handle;
		efi_simple_text_output_protocol_t *con_out;
		unsigned long stderr_handle;
		unsigned long stderr;
		void *runtime;      // efi_runtime_services_t
		void *boottime;     // efi_boot_services_t
		unsigned long nr_tables;
		unsigned long tables;
	};
	efi_system_table_32_t mixed_mode;
} efi_system_table_t;


struct efi_boot_memmap {
	unsigned long		map_size;
	unsigned long		desc_size;
	u32					desc_ver;
	unsigned long		map_key;
	unsigned long		buff_size;
	efi_memory_desc_t	map[];
};

struct efi_unaccepted_memory {
	u32 version;
	u32 unit_size;
	u64 phys_base;
	u64 size;
	unsigned long bitmap[];
};


//************************************************
//EFI_TIME
//************************************************
// This represents the current time information
typedef struct {
   u16    Year;              // 1900 - 9999
   u8     Month;             // 1 - 12
   u8     Day;               // 1 - 31
   u8     Hour;              // 0 - 23
   u8     Minute;            // 0 - 59
   u8     Second;            // 0 - 59
   u8     Pad1;
   u32    Nanosecond;        // 0 - 999,999,999
   s16    TimeZone;          // —1440 to 1440 or 2047
   u8     Daylight;
   u8     Pad2;
} EFI_TIME;

//***************************************************
// Bit Definitions for EFI_TIME.Daylight. See below.
//***************************************************
#define EFI_TIME_ADJUST_DAYLIGHT   0x01
#define EFI_TIME_IN_DAYLIGHT       0x02

//***************************************************
// Value Definition for EFI_TIME.TimeZone. See below.
//***************************************************
#define EFI_UNSPECIFIED_TIMEZONE   0x07FF

//******************************************************
// EFI_TIME_CAPABILITIES
//******************************************************
// This provides the capabilities of the
// real time clock device as exposed through the EFI
// interfaces.
typedef struct {
   	u32 		Resolution;
   	u32         Accuracy;
   	efi_bool_t  SetsToZero;
} EFI_TIME_CAPABILITIES;


#define EFI_RUNTIME_SERVICES_SIGNATURE 0x56524553544e5552
typedef struct {
	efi_table_hdr_t Hdr;
	efi_status_t (*GetTime)(
					EFI_TIME *Time,
					EFI_TIME_CAPABILITIES *Capabilities);
	efi_status_t (*SetTime)(EFI_TIME *Time);
	void *GetWakeupTime;
	void *SetWakeupTime;
	efi_status_t (*SetVirtualAddressMap)(
					u32 MemoryMapSize,
					u32 DescriptorSize,
					u32 DescriptorVersion,
					efi_memory_desc_t *VirtualMap);
	void *ConvertPointer;
	void *GetVariable;
	void *GetNextVariableName;
	void *SetVariable;
	efi_status_t (*GetNextHighMonotonicCount)(uint32_t *HighCount);
	void *ResetSystem;
	void *UpdateCapsule;
	void *QueryCapsuleCapabilities;
	void *QueryVariableInfo;
} EFI_RUNTIME_SERVICES;

#endif
