#include <stdbool.h>
#include <string.h>
#include <lilac/log.h>
#include <lilac/device.h>
#include <drivers/ahci.h>
#include <drivers/blkdev.h>
#include <mm/kmm.h>
#include <mm/kmalloc.h>
#include <utility/ata.h>

#define	SATA_SIG_ATA	0x00000101	// SATA drive
#define	SATA_SIG_ATAPI	0xEB140101	// SATAPI drive
#define	SATA_SIG_SEMB	0xC33C0101	// Enclosure management bridge
#define	SATA_SIG_PM	    0x96690101	// Port multiplier

#define NUM_PRDT_ENTRIES 8
#define CMD_LIST_SZ 32
#define NUM_CMD_SLOTS 32

#define AHCI_DEV_NULL 0
#define AHCI_DEV_SATA 1
#define AHCI_DEV_SEMB 2
#define AHCI_DEV_PM 3
#define AHCI_DEV_SATAPI 4

#define HBA_PORT_IPM_ACTIVE 1
#define HBA_PORT_DET_PRESENT 3

#define HBA_PxCMD_ST    0x0001
#define HBA_PxCMD_FRE   0x0010
#define HBA_PxCMD_FR    0x4000
#define HBA_PxCMD_CR    0x8000

#define HBA_PxIS_TFES   (1 << 30)

#define get_clb(portnum) \
	((void*)(ahci_base + (portnum << 10)))
#define get_fb(portnum) \
	((void*)(ahci_base + (num_ports << 10) + (portnum << 8)))
#define get_cmdtbl(portnum, cmdslot) \
	((void*)(ahci_base + (num_ports << 10) \
	 + (num_ports << 8) + (portnum << 13) + (cmdslot << 8)))

struct ahci_device {
	hba_port_t *port;
	int portno;
	int type;
};

const struct disk_operations ahci_ops = {
	.disk_read = ahci_read,
	.disk_write = ahci_write,
};

static hba_mem_t *abar;
static int num_ports;
static int num_devices;

static uintptr_t ahci_base;
static struct ahci_device *devices;

static void ahci_install_device(struct ahci_device *dev);
static int check_type(hba_port_t *port);
static void port_mem_init(int num_ports);
static int find_cmdslot(hba_port_t *port);
static int __ahci_read(struct ahci_device *dev, u32 startl, u32 starth,
	u32 count, u16 *buf);
static int __ahci_write(struct ahci_device *dev, u32 startl, u32 starth,
	u32 count, u16 *buf);

// Initialize AHCI controller
void ahci_init(hba_mem_t *abar_phys)
{
	int size;
	u32 pi;
	int i = 0;

	map_to_self(abar_phys, PAGE_SIZE, PG_WRITE | PG_CACHE_DISABLE);
	abar = abar_phys;

	pi = abar->pi;
	while (i < 32) {
		if (pi & 1) {
			int dt = check_type(&abar->ports[i]);
			if (dt == AHCI_DEV_SATA) {
				klog(LOG_INFO, "SATA drive found at port %d\n", i);
				devices = krealloc(devices, (num_devices+1) * sizeof(*devices));
				devices[num_devices].port = &abar->ports[i];
				devices[num_devices].portno = i;
				devices[num_devices++].type = dt;
			}
			else if (dt == AHCI_DEV_SATAPI)
				klog(LOG_INFO, "SATAPI drive found at port %d\n", i);
			else if (dt == AHCI_DEV_SEMB)
				klog(LOG_INFO, "SEMB drive found at port %d\n", i);
			else if (dt == AHCI_DEV_PM)
				klog(LOG_INFO, "PM drive found at port %d\n", i);
			num_ports++;
		}
		pi >>= 1;
		i++;
	}

	if (num_ports == 0) {
		printf("No AHCI drives found\n");
		return;
	}

	size = num_ports * sizeof(hba_port_t) + sizeof(*abar);
	if (size > PAGE_SIZE) {
		unmap_from_self(abar, PAGE_SIZE);
		map_to_self(abar_phys, size, PG_WRITE | PG_CACHE_DISABLE);
	}

	port_mem_init(num_ports);
	for (i = 0; i < num_ports; i++)
		ahci_port_rebase(&abar->ports[i], i);

	for (i = 0; i < num_devices; i++)
		ahci_install_device(&devices[i]);

	kstatus(STATUS_OK, "AHCI controller initialized\n");
}

// Check device type
static int check_type(hba_port_t *port)
{
	u32 ssts = port->ssts;

	u8 ipm = (ssts >> 8) & 0x0F;
	u8 det = ssts & 0x0F;

	if (det != HBA_PORT_DET_PRESENT)	// Check drive status
		return AHCI_DEV_NULL;
	if (ipm != HBA_PORT_IPM_ACTIVE)
		return AHCI_DEV_NULL;

	switch (port->sig) {
	case SATA_SIG_ATAPI:
		return AHCI_DEV_SATAPI;
	case SATA_SIG_SEMB:
		return AHCI_DEV_SEMB;
	case SATA_SIG_PM:
		return AHCI_DEV_PM;
	default:
		return AHCI_DEV_SATA;
	}
}

static void port_mem_init(int num_ports)
{
	int size = sizeof(struct HBA_CMD_HEADER) * CMD_LIST_SZ * num_ports
		+ sizeof(struct HBA_FIS) * num_ports
		+ sizeof(struct HBA_CMD_TBL) * CMD_LIST_SZ * num_ports
		+ sizeof(struct HBA_PRDT_ENTRY) * NUM_PRDT_ENTRIES * CMD_LIST_SZ * num_ports;

	ahci_base = (uintptr_t)kvirtual_alloc(size, PG_WRITE | PG_CACHE_DISABLE);
}

void ahci_port_rebase(hba_port_t *port, int portno)
{
	stop_cmd(port);	// Stop command engine

	// Command list offset: 1K*portno
	port->clb = virt_to_phys(get_clb(portno));
	port->clbu = 0;
	memset(get_clb(portno), 0, 1024);

	// FIS entry size = 256 bytes per port
	port->fb = virt_to_phys(get_fb(portno));
	port->fbu = 0;
	memset(get_fb(portno), 0, 256);

	// Command table size = 256*32 = 8K per port
	hba_cmd_header_t *cmdheader = (hba_cmd_header_t*)(get_clb(portno));
	for (int i = 0; i < NUM_CMD_SLOTS; i++) {
		cmdheader[i].prdtl = NUM_PRDT_ENTRIES;
		// 256 bytes per command table, 64+16+48+16*8
		// Command table offset: 40K + 8K*portno + cmdheader_index*256
		cmdheader[i].ctba = virt_to_phys(get_cmdtbl(portno, i));
		cmdheader[i].ctbau = 0;
		memset(get_cmdtbl(portno, i), 0, 256);
	}

	ahci_start_cmd(port);	// Start command engine
}

static void ahci_install_device(struct ahci_device *dev)
{
	struct gendisk *new_disk = kzmalloc(sizeof(*new_disk));
	strcpy(new_disk->driver, "AHCI");

	new_disk->major = SATA_DEVICE;
	new_disk->first_minor = dev->portno * 16;
	new_disk->ops = &ahci_ops;
	new_disk->private = dev;

	add_gendisk(new_disk);
}

// Start command engine
void ahci_start_cmd(hba_port_t *port)
{
	// Wait until CR (bit15) is cleared
	while (port->cmd & HBA_PxCMD_CR)
		;

	// Set FRE (bit4) and ST (bit0)
	port->cmd |= HBA_PxCMD_FRE;
	port->cmd |= HBA_PxCMD_ST;

}

// Stop command engine
void stop_cmd(hba_port_t *port)
{
	// Clear ST (bit0)
	port->cmd &= ~HBA_PxCMD_ST;

	// Clear FRE (bit4)
	port->cmd &= ~HBA_PxCMD_FRE;

	// Wait until FR (bit14), CR (bit15) are cleared
	while(1) {
		if (port->cmd & HBA_PxCMD_FR)
			continue;
		if (port->cmd & HBA_PxCMD_CR)
			continue;
		break;
	}
}

int ahci_read(struct gendisk *disk, u64 lba, void *buf, u32 count)
{
	// printf("Reading %d sectors from disk at LBA %x\n", count, lba);
	return __ahci_read(disk->private, lba & 0xFFFFFFFF, lba >> 32, count,
		(void*)virt_to_phys(buf));
}

int ahci_write(struct gendisk *disk, u64 lba, const void *buf, u32 count)
{
	// printf("Writing %d sectors to disk at LBA %x\n", count, lba);
	return __ahci_write(disk->private, lba & 0xFFFFFFFF, lba >> 32, count,
		(void*)virt_to_phys((void*)buf));
}

// DMA - Read
// Buf is a physical address
static int __ahci_read(struct ahci_device *dev, u32 startl, u32 starth,
	u32 count, u16 *buf)
{
	hba_port_t *port = dev->port;
	int i = 0;
	u32 sec_rem = count;
	port->is = (u32) -1;		// Clear pending interrupt bits
	int spin = 0; 				// Spin lock timeout counter
	int slot = find_cmdslot(port);
	if (slot == -1)
		return 1;

	hba_cmd_header_t *cmdheader = (hba_cmd_header_t*)get_clb(dev->portno);
	cmdheader += slot;
	cmdheader->cfl = sizeof(fis_reg_h2d_t)/sizeof(u32);	// Command FIS size
	cmdheader->w = 0;		// Read from device
	cmdheader->prdtl = (u16)((count-1)>>4) + 1;	// PRDT entries count

	hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t*)get_cmdtbl(dev->portno, slot);
	memset(cmdtbl, 0, sizeof(*cmdtbl) +
 		(cmdheader->prdtl)*sizeof(hba_prdt_entry_t));

	// 8K bytes (16 sectors) per PRDT
	for (i = 0; i < cmdheader->prdtl-1; i++)
	{
		cmdtbl->prdt_entry[i].dba = (u32)buf;
		cmdtbl->prdt_entry[i].dbc = 8 * 1024 - 1;	// 8K bytes
		// cmdtbl->prdt_entry[i].i = 1;
		buf += 0x1000;	// 4K words
		sec_rem -= 16;	// 16 sectors
	}
	// Last entry
	if (sec_rem > 0) {
		cmdtbl->prdt_entry[i].dba = (u32)buf;
		cmdtbl->prdt_entry[i].dbc = (sec_rem<<9)-1;	// 512 bytes per sector
		// cmdtbl->prdt_entry[i].i = 1;
	}

	// Setup command
	fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t*)(&cmdtbl->cfis);

	cmdfis->fis_type = FIS_TYPE_REG_H2D;
	cmdfis->c = 1;	// Command
	cmdfis->command = ATA_CMD_READ_DMA_EX;

	cmdfis->lba0 = (u8)startl;
	cmdfis->lba1 = (u8)(startl>>8);
	cmdfis->lba2 = (u8)(startl>>16);
	cmdfis->device = 1<<6;	// LBA mode

	cmdfis->lba3 = (u8)(startl>>24);
	cmdfis->lba4 = (u8)starth;
	cmdfis->lba5 = (u8)(starth>>8);

	cmdfis->countl = count & 0xFF;
	cmdfis->counth = (count >> 8) & 0xFF;

	// The below loop waits until the port is no longer busy before issuing a new command
	while ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000)
		spin++;
	if (spin == 1000000) {
		printf("Port is hung\n");
		return 1;
	}

	port->ci = 1<<slot;	// Issue command

	// Wait for completion
	while (1)
	{
		// In some longer duration reads, it may be helpful to spin on the DPS bit
		// in the PxIS port field as well (1 << 5)
		if ((port->ci & (1<<slot)) == 0)
			break;
		if (port->is & HBA_PxIS_TFES)	// Task file error
		{
			printf("Read disk error\n");
			return 1;
		}
	}

	// Check again
	if (port->is & HBA_PxIS_TFES)
	{
		printf("Read disk error\n");
		return 1;
	}

	return 0;
}


static int __ahci_write(struct ahci_device *dev, u32 startl, u32 starth,
	u32 count, u16 *buf)
{
	hba_port_t *port = dev->port;
	u32 sec_rem = count;
	port->is = ~0x0UL;		// Clear pending interrupt bits
	int i = 0;
	int spin = 0; 				// Spin lock timeout counter
	int slot = find_cmdslot(port);
	if (slot == -1)
		return 1;

	hba_cmd_header_t *cmdheader = (hba_cmd_header_t*)get_clb(dev->portno);
	cmdheader += slot;
	cmdheader->cfl = sizeof(fis_reg_h2d_t)/sizeof(u32);	// Command FIS size
	cmdheader->w = 1;		// Write to device
	cmdheader->prdtl = (u16)((count-1)>>4) + 1;	// PRDT entries count

	hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t*)get_cmdtbl(dev->portno, slot);
	memset(cmdtbl, 0, sizeof(*cmdtbl) +
 		(cmdheader->prdtl)*sizeof(hba_prdt_entry_t));

	// 8K bytes (16 sectors) per PRDT
	for (i = 0; i < cmdheader->prdtl-1; i++)
	{
		cmdtbl->prdt_entry[i].dba = (u32)buf;
		cmdtbl->prdt_entry[i].dbc = 8 * 1024 - 1;	// 8K bytes
		// cmdtbl->prdt_entry[i].i = 1;
		buf += 0x1000;	// 4K words
		sec_rem -= 16;	// 16 sectors
	}
	// Last entry
	if (sec_rem > 0) {
		cmdtbl->prdt_entry[i].dba = (u32)buf;
		cmdtbl->prdt_entry[i].dbc = (sec_rem<<9)-1;	// 512 bytes per sector
		// cmdtbl->prdt_entry[i].i = 1;
	}

	// Setup command
	fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t*)(&cmdtbl->cfis);

	cmdfis->fis_type = FIS_TYPE_REG_H2D;
	cmdfis->c = 1;	// Command
	cmdfis->command = ATA_CMD_WRITE_DMA_EX;

	cmdfis->lba0 = (u8)startl;
	cmdfis->lba1 = (u8)(startl>>8);
	cmdfis->lba2 = (u8)(startl>>16);
	cmdfis->device = 1<<6;	// LBA mode

	cmdfis->lba3 = (u8)(startl>>24);
	cmdfis->lba4 = (u8)starth;
	cmdfis->lba5 = (u8)(starth>>8);

	cmdfis->countl = count & 0xFF;
	cmdfis->counth = (count >> 8) & 0xFF;

	// The below loop waits until the port is no longer busy before issuing a new command
	while ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000)
		spin++;
	if (spin == 1000000) {
		printf("Port is hung\n");
		return 1;
	}

	port->ci = 1<<slot;	// Issue command

	// Wait for completion
	while (1) {
		// In some longer duration reads, it may be helpful to spin on the DPS bit
		// in the PxIS port field as well (1 << 5)
		if ((port->ci & (1<<slot)) == 0)
			break;
		if (port->is & HBA_PxIS_TFES)	// Task file error
		{
			printf("Write disk error\n");
			return 1;
		}
	}

	// Check again
	if (port->is & HBA_PxIS_TFES) {
		printf("Write disk error\n");
		return 1;
	}

	return 0;
}


// Find a free command list slot
static int find_cmdslot(hba_port_t *port)
{
	// If not set in SACT and CI, the slot is free
	u32 slots = (port->sact | port->ci);
	for (int i=0; i < NUM_CMD_SLOTS; i++)
	{
		if ((slots&1) == 0)
			return i;
		slots >>= 1;
	}
	printf("Cannot find free command list entry\n");
	return -1;
}
