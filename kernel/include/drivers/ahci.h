#ifndef _AHCI_H
#define _AHCI_H

#include <lilac/types.h>
#include <drivers/blkdev.h>
#include <drivers/sata.h>

typedef volatile struct HBA_PORT {
	u32 clb;		// 0x00, command list base address, 1K-byte aligned
	u32 clbu;		// 0x04, command list base address upper 32 bits
	u32 fb;		// 0x08, FIS base address, 256-byte aligned
	u32 fbu;		// 0x0C, FIS base address upper 32 bits
	u32 is;		// 0x10, interrupt status
	u32 ie;		// 0x14, interrupt enable
	u32 cmd;		// 0x18, command and status
	u32 rsv0;		// 0x1C, Reserved
	u32 tfd;		// 0x20, task file data
	u32 sig;		// 0x24, signature
	u32 ssts;		// 0x28, SATA status (SCR0:SStatus)
	u32 sctl;		// 0x2C, SATA control (SCR2:SControl)
	u32 serr;		// 0x30, SATA error (SCR1:SError)
	u32 sact;		// 0x34, SATA active (SCR3:SActive)
	u32 ci;		// 0x38, command issue
	u32 sntf;		// 0x3C, SATA notification (SCR4:SNotification)
	u32 fbs;		// 0x40, FIS-based switch control
	u32 rsv1[11];	// 0x44 ~ 0x6F, Reserved
	u32 vendor[4];	// 0x70 ~ 0x7F, vendor specific
} hba_port_t;

typedef volatile struct HBA_MEM {
	// 0x00 - 0x2B, Generic Host Control
	u32 cap;		// 0x00, Host capability
	u32 ghc;		// 0x04, Global host control
	u32 is;		// 0x08, Interrupt status
	u32 pi;		// 0x0C, Port implemented
	u32 vs;		// 0x10, Version
	u32 ccc_ctl;	// 0x14, Command completion coalescing control
	u32 ccc_pts;	// 0x18, Command completion coalescing ports
	u32 em_loc;	// 0x1C, Enclosure management location
	u32 em_ctl;	// 0x20, Enclosure management control
	u32 cap2;		// 0x24, Host capabilities extended
	u32 bohc;		// 0x28, BIOS/OS handoff control and status

	// 0x2C - 0x9F, Reserved
	u8  rsv[0xA0-0x2C];

	// 0xA0 - 0xFF, Vendor specific registers
	u8  vendor[0x100-0xA0];

	// 0x100 - 0x10FF, Port control registers
	hba_port_t	ports[];	// 1 ~ 32
} hba_mem_t;

typedef volatile struct HBA_FIS {
	// 0x00
	fis_dma_setup_t	dsfis;		// DMA Setup FIS
	u8         pad0[4];

	// 0x20
	fis_pio_setup_t	psfis;		// PIO Setup FIS
	u8         pad1[12];

	// 0x40
	fis_reg_d2h_t	rfis;		// Register – Device to Host FIS
	u8         pad2[4];

	// 0x58
	u16 /* FIS_DEV_BITS */	sdbfis;		// Set Device Bit FIS

	// 0x60
	u8         ufis[64];

	// 0xA0
	u8   	rsv[0x100-0xA0];
} hba_fis_t;

typedef struct HBA_CMD_HEADER {
	// DW0
	u8  cfl:5;		// Command FIS length in DWORDS, 2 ~ 16
	u8  a:1;		// ATAPI
	u8  w:1;		// Write, 1: H2D, 0: D2H
	u8  p:1;		// Prefetchable

	u8  r:1;		// Reset
	u8  b:1;		// BIST
	u8  c:1;		// Clear busy upon R_OK
	u8  rsv0:1;	// Reserved
	u8  pmp:4;		// Port multiplier port

	u16 prdtl;		// Physical region descriptor table length in entries

	// DW1
	volatile
	u32 prdbc;		// Physical region descriptor byte count transferred

	// DW2, 3
	u32 ctba;		// Command table descriptor base address
	u32 ctbau;		// Command table descriptor base address upper 32 bits

	// DW4 - 7
	u32 rsv1[4];	// Reserved
} hba_cmd_header_t;

typedef struct HBA_PRDT_ENTRY {
	u32 dba;		// Data base address
	u32 dbau;		// Data base address upper 32 bits
	u32 rsv0;		// Reserved

	// DW3
	u32 dbc:22;	// Byte count, 4M max
	u32 rsv1:9;	// Reserved
	u32 i:1;		// Interrupt on completion
} hba_prdt_entry_t;

typedef struct HBA_CMD_TBL {
	// 0x00
	u8  cfis[64];	// Command FIS

	// 0x40
	u8  acmd[16];	// ATAPI command, 12 or 16 bytes

	// 0x50
	u8  rsv[48];	// Reserved

	// 0x80
	// Physical region descriptor table entries, 0 ~ 65535
	hba_prdt_entry_t	prdt_entry[];
} hba_cmd_tbl_t;


void ahci_init(hba_mem_t *abar);
void ahci_port_rebase(hba_port_t *port, int portno);
void ahci_start_cmd(hba_port_t *port);
void stop_cmd(hba_port_t *port);

int ahci_read(struct gendisk *disk, u64 lba, void *buf, u32 count);
int ahci_write(struct gendisk *disk, u64 lba, const void *buf, u32 count);

#endif
