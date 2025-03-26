DEFAULT_HOST=$(shell ../scripts/default-host.sh)
HOST?=$(DEFAULT_HOST)
HOSTARCH=$(shell ../scripts/target-triplet-to-arch.sh $(HOST))

CFLAGS?=-O2 -g
LDFLAGS?=
LIBS?=

DESTDIR?=
PREFIX?=/usr/local
EXEC_PREFIX?=$(PREFIX)
INCLUDEDIR?=$(PREFIX)/include
LIBDIR?=$(EXEC_PREFIX)/lib

CFLAGS:=$(CFLAGS) -ffreestanding -Wall -Wextra -Iinclude -Wno-unused-parameter
LIBK_CFLAGS:=$(CFLAGS) -D__is_libk -I ../kernel/include

ARCHDIR=arch/$(HOSTARCH)

include $(ARCHDIR)/make.config

CFLAGS:=$(CFLAGS) $(ARCH_CFLAGS)
LIBK_CFLAGS:=$(LIBK_CFLAGS) $(KERNEL_ARCH_CFLAGS)

FREEOBJS=\
$(ARCH_FREEOBJS) \
stdio/printf.o \
stdio/vprintf.o \
stdio/putchar.o \
stdio/puts.o \
stdlib/abort.o \
string/memcmp.o \
string/memcpy.o \
string/memmove.o \
string/memset.o \
string/strcpy.o \
string/strcmp.o \
string/strcat.o \
string/strlen.o \
string/strchr.o \
string/strdup.o \
math/log2.o \
ctype/ctype.o \
errno.o

HOSTEDOBJS=\
$(ARCH_HOSTEDOBJS) \
unistd/fork.o \
unistd/exec.o \
unistd/_exit.o \
unistd/read.o \
unistd/write.o \
unistd/close.o \
unistd/getcwd.o \
fcntl/open.o \
dirent/readdir.o \
sys/syscall.o \
sys/wait.o \
stdio/sscanf.o \
stdio/vsscanf.o \
stdlib/exit.o


OBJS=\
$(FREEOBJS) \
$(HOSTEDOBJS) \

LIBK_OBJS=$(FREEOBJS:.o=.libk.o)

BINARIES=libc.a libk.a

.PHONY: all clean install install-headers install-libs
.SUFFIXES: .o .libk.o .c .S

all: $(BINARIES)

libc.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

libk.a: $(LIBK_OBJS)
	$(AR) rcs $@ $(LIBK_OBJS)

.c.o:
	$(CC) -MD -c $< -o $@ $(CFLAGS)

.c.S:
	$(CC) -MD -c $< -o $@ $(CFLAGS)

.c.libk.o:
	$(CC) -MD -c $< -o $@ $(LIBK_CFLAGS)

.S.libk.o:
	$(CC) -MD -c $< -o $@ $(LIBK_CFLAGS)

clean:
	rm -f $(BINARIES) *.a
	rm -f $(OBJS) $(LIBK_OBJS) *.o */*.o */*/*.o
	rm -f $(OBJS:.o=.d) $(LIBK_OBJS:.o=.d) *.d */*.d */*/*.d

install: install-headers install-libs

install-headers:
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	cp -Rp include/. $(DESTDIR)$(INCLUDEDIR)/.

install-libs: libc.a libk.a
	mkdir -p $(DESTDIR)$(LIBDIR)
	mkdir -p $(DESTDIR)$(BOOTDIR)
	cp libc.a $(DESTDIR)$(LIBDIR)
	cp libk.a $(DESTDIR)$(BOOTDIR)

-include $(OBJS:.o=.d)
-include $(LIBK_OBJS:.o=.d)
