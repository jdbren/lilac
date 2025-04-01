CFLAGS?=-O2
LDFLAGS?=
LIBS?=./kernel/libk.a

DESTDIR?=
PREFIX?=/usr/local
EXEC_PREFIX?=$(PREFIX)
BOOTDIR?=$(EXEC_PREFIX)/boot
INCLUDEDIR?=$(PREFIX)/include

CFLAGS:=$(CFLAGS) -I../../include -Wno-unused-parameter \
	-Wno-unterminated-string-initialization -fno-analyzer
LDFLAGS:=$(LDFLAGS)
LIBS:=$(LIBS) -nostdlib -lk -lgcc

CFLAGS:=$(CFLAGS) $(KERNEL_ARCH_CFLAGS)

ACPI_OBJS=\
acpi.o \
madt.o \
hpet.o \
osl.o \
$(patsubst %.c,%.o,$(wildcard acpica/*.c))

OBJS=\
$(ACPI_OBJS)

all: acpi.lib

acpi.lib: $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) -MD -c $< -o $@ $(CFLAGS)

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) acpi.lib

-include $(OBJS:.o=.d)
