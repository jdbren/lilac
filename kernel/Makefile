HOST?=$(shell uname -m)-elf
HOSTARCH=$(shell ../scripts/target-triplet-to-arch.sh $(HOST))
COMPILER_DIR?=$(shell dirname $(shell which $(CC)))/..

ifeq ($(HOST), x86_64-elf)
CONFIG_x86_64-elf := 1
endif

CFLAGS?=-Og
LDFLAGS?=
LIBS?=./libk.a ./drivers/acpi/acpi.lib

DESTDIR?=
PREFIX?=/usr/local
EXEC_PREFIX?=$(PREFIX)
BOOTDIR?=$(EXEC_PREFIX)/boot
INCLUDEDIR?=$(PREFIX)/include

FLAGS=-D__lilac__ -D__KERNEL__ -Iinclude -ffreestanding -Wall -Wextra \
	-Wno-unused-parameter -Wno-unused-function -Wno-unused-variable
CFLAGS:=$(CFLAGS) $(FLAGS) -fno-strict-aliasing -mno-red-zone -fanalyzer -fstack-protector 
# -DDEBUG_VFS -DDEBUG_FORK -DDEBUG_KMM -DDEBUG_KMALLOC -DDEBUG_PAGING -DDEBUG_FAT -DDEBUG_TMPFS -DDEBUG_ELF
CXXFLAGS:=$(CXXFLAGS) $(FLAGS) -fno-exceptions -fno-rtti
LDFLAGS:=$(LDFLAGS)
LIBS:=$(LIBS) -nostdlib $(COMPILER_DIR)/lib/gcc/*/*/libgcc.a

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
kernel/pipe.o \
kernel/process.o \
kernel/reboot.o \
kernel/sched.o \
kernel/signal.o \
kernel/sync.o \
kernel/syscall.o \
kernel/wait.o \
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
fs/stat.o \
fs/name_utils.o \
drivers/blkdev.o \
drivers/pci.o \
drivers/ahci.o \
drivers/framebuffer.o \
drivers/vga.o \
lib/icxxabi.o \
lib/multiboot.o \
lib/rbtree.o

LIBK_OBJS=$(patsubst %.c,%.o,$(wildcard lib/*/*.c))

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

lilac.ker: $(OBJS) $(LINKER_SCRIPT) acpi libk.a
ifndef VERBOSE
	@echo "\tLD\t$@"
	@$(LD) -T $(LINKER_SCRIPT) -o $@ $(LINK_LIST)
else
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LINK_LIST)
endif
	grub-file --is-x86-multiboot2 lilac.ker

$(ARCHDIR)/crtbegin.o $(ARCHDIR)/crtend.o:
ifndef VERBOSE
	@echo "\tCP\t$@"
	@OBJ=`$(CC) $(CFLAGS) $(LDFLAGS) -print-file-name=$(@F)` && cp "$$OBJ" $@
else
	OBJ=`$(CC) $(CFLAGS) $(LDFLAGS) -print-file-name=$(@F)` && cp "$$OBJ" $@
endif

libk.a: $(LIBK_OBJS)
ifndef VERBOSE
	@echo "\tAR\t$@"
	@$(AR) rcs $@ $(LIBK_OBJS)
else
	$(AR) rcs $@ $(LIBK_OBJS)
endif

.c.o:
ifndef VERBOSE
	@echo "\tCC\t$<"
	@$(CC) -MD -c $< -o $@ $(CFLAGS)
else
	$(CC) -MD -c $< -o $@ $(CFLAGS)
endif

.S.o:
ifndef VERBOSE
	@echo "\tCC\t$<"
	@$(CC) -MD -c $< -o $@ $(CFLAGS)
else
	$(CC) -MD -c $< -o $@ $(CFLAGS)
endif

.s.o:
ifndef VERBOSE
	@echo "\tAS\t$<"
	@$(AS) -c $< -o $@
else
	$(AS) -c $< -o $@
endif

.cpp.o:
ifndef VERBOSE
	@echo "\tCXX\t$<"
	@$(CXX) -MD -c $< -o $@ $(CXXFLAGS)
else
	$(CXX) -MD -c $< -o $@ $(CXXFLAGS)
endif

clean:
	rm -f lilac.ker
	rm -f libk.a
	@echo "CLEAN\tOBJS"
	@rm -f $(OBJS)
	@rm -f $(OBJS:.o=.d)
	@echo "CLEAN\tLIBK_OBJS"
	@rm -f $(LIBK_OBJS)
	@rm -f $(LIBK_OBJS:.o=.d)
	@$(MAKE) -C drivers/acpi clean

install: lilac.ker
	mkdir -p $(DESTDIR)$(BOOTDIR)
	cp lilac.ker $(DESTDIR)$(BOOTDIR)

acpi: libk.a
	$(MAKE) -C drivers/acpi

-include $(OBJS:.o=.d)
