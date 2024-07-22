DEFAULT_HOST=$(shell ../scripts/default-host.sh)
HOST?=$(DEFAULT_HOST)
HOSTARCH=$(shell ../scripts/target-triplet-to-arch.sh $(HOST))

CFLAGS?=-Og
LDFLAGS?=
LIBS?=./drivers/acpi/acpi.lib ../libc/libk.a

DESTDIR?=
PREFIX?=/usr/local
EXEC_PREFIX?=$(PREFIX)
BOOTDIR?=$(EXEC_PREFIX)/boot
INCLUDEDIR?=$(PREFIX)/include

CFLAGS:=$(CFLAGS) -D __lilac__ -D __is_kernel -I include \
-ffreestanding -Wall -Wextra -mno-red-zone
LDFLAGS:=$(LDFLAGS)
LIBS:=$(LIBS) -nostdlib -lgcc

ARCHDIR=arch/$(HOSTARCH)

include $(ARCHDIR)/make.config

CFLAGS:=$(CFLAGS) $(KERNEL_ARCH_CFLAGS)
LDFLAGS:=$(LDFLAGS) $(KERNEL_ARCH_LDFLAGS)
LIBS:=$(LIBS) $(KERNEL_ARCH_LIBS)

ACPI_OBJS=\
drivers/acpi/acpi.o \
drivers/acpi/madt.o \
drivers/acpi/hpet.o \
drivers/acpi/osl.o \
$(patsubst %.c,%.o,$(wildcard drivers/acpi/acpica/*.c))

KERNEL_OBJS=\
$(KERNEL_ARCH_OBJS) \
kernel/kmain.o \
kernel/log.o \
kernel/process.o \
kernel/sched.o \
kernel/sync.o \
kernel/open.o \
kernel/write.o \
mm/kmalloc.o \
mm/mm.o \
fs/fat32.o \
fs/vfs.o \
fs/gpt.o \
drivers/blkdev.o \
drivers/pci.o \
drivers/ahci.o

OBJS=\
$(ARCHDIR)/boot/crti.o \
$(ARCHDIR)/crtbegin.o \
$(KERNEL_OBJS) \
$(ARCHDIR)/crtend.o \
$(ARCHDIR)/boot/crtn.o \

LINK_LIST=\
$(LDFLAGS) \
$(ARCHDIR)/boot/crti.o \
$(ARCHDIR)/crtbegin.o \
$(KERNEL_OBJS) \
$(LIBS) \
$(ARCHDIR)/crtend.o \
$(ARCHDIR)/boot/crtn.o \

.PHONY: all clean install install-headers install-kernel acpi
.SUFFIXES: .o .c .S

all: lilac.kernel

lilac.kernel: $(OBJS) $(ARCHDIR)/linker.ld acpi
	$(CC) -T $(ARCHDIR)/linker.ld -o $@ $(CFLAGS) $(LINK_LIST)
	grub-file --is-x86-multiboot2 lilac.kernel

$(ARCHDIR)/crtbegin.o $(ARCHDIR)/crtend.o:
	OBJ=`$(CC) $(CFLAGS) $(LDFLAGS) -print-file-name=$(@F)` && cp "$$OBJ" $@

.c.o:
	$(CC) -MD -c $< -o $@ $(CFLAGS)

.S.o:
	$(CC) -MD -c $< -o $@ $(CFLAGS)

clean:
	rm -f lilac.kernel
	rm -f $(OBJS)
	rm -f $(OBJS:.o=.d)
	$(MAKE) -C drivers/acpi clean

install: lilac.kernel
	mkdir -p $(DESTDIR)$(BOOTDIR)
	cp lilac.kernel $(DESTDIR)$(BOOTDIR)

acpi:
	$(MAKE) -C drivers/acpi

-include $(OBJS:.o=.d)
