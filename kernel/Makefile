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

FLAGS=-D__lilac__ -D__is_kernel -Iinclude -ffreestanding -Wall -Wextra \
	-Wno-unused-parameter -Wno-unused-function -Wno-unused-variable
CFLAGS:=$(CFLAGS) $(FLAGS) -mno-red-zone
CXXFLAGS:=$(CXXFLAGS) $(FLAGS) -fno-exceptions -fno-rtti
LDFLAGS:=$(LDFLAGS)
LIBS:=$(LIBS) -nostdlib -lgcc

ARCHDIR=arch/$(HOSTARCH)

ifdef CONFIG_x86_64-elf
include $(ARCHDIR)/make64.config
else
include $(ARCHDIR)/make32.config
endif

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
kernel/console.o \
kernel/device.o \
kernel/log.o \
kernel/process.o \
kernel/sched.o \
kernel/sync.o \
mm/kmalloc.o \
mm/mm.o \
fs/fat32/fat32.o \
fs/fat32/dir.o \
fs/fat32/file.o \
fs/fat32/inode.o \
fs/fat32/open.o \
fs/tmpfs/tmpfs.o \
fs/tmpfs/dir.o \
fs/tmpfs/file.o \
fs/tmpfs/sb.o \
fs/vfs.o \
fs/sb.o \
fs/file.o \
fs/inode.o \
fs/dcache.o \
fs/mount.o \
fs/name_utils.o \
drivers/blkdev.o \
drivers/pci.o \
drivers/ahci.o \
drivers/framebuffer.o \
drivers/vga.o \
lib/icxxabi.o \
lib/multiboot.o

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
$(ARCHDIR)/boot/crtn.o


.PHONY: all clean install install-headers install-kernel acpi
.SUFFIXES: .o .c .S .cpp

all: lilac.ker

lilac.ker: $(OBJS) $(LINKER_SCRIPT) acpi
	$(CC) -T $(LINKER_SCRIPT) -o $@ $(CFLAGS) $(LINK_LIST)
	grub-file --is-x86-multiboot2 lilac.ker

$(ARCHDIR)/crtbegin.o $(ARCHDIR)/crtend.o:
	OBJ=`$(CC) $(CFLAGS) $(LDFLAGS) -print-file-name=$(@F)` && cp "$$OBJ" $@

.c.o:
	$(CC) -MD -c $< -o $@ $(CFLAGS)

.S.o:
	$(CC) -MD -c $< -o $@ $(CFLAGS)

.cpp.o:
	$(CXX) -MD -c $< -o $@ $(CXXFLAGS)

clean:
	rm -f lilac.ker
	rm -f $(OBJS)
	rm -f $(OBJS:.o=.d)
	$(MAKE) -C drivers/acpi clean

install: lilac.ker
	mkdir -p $(DESTDIR)$(BOOTDIR)
	cp lilac.ker $(DESTDIR)$(BOOTDIR)

acpi:
	$(MAKE) -C drivers/acpi

-include $(OBJS:.o=.d)
