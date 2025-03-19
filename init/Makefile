CFLAGS=-ffreestanding -I ../libc/include -nostdlib -lgcc
ifdef CONFIG_x86_64-elf
CFLAGS+=-DARCH_x86_64 -mgeneral-regs-only
endif

OBJS=crt0.o main.o
LIBS=../libc/libc.a

.PHONY: all clean install
.SUFFIXES: .o .c .S
all: init

init: $(OBJS)
	$(LD) -o $@ $(OBJS) $(LIBS)

.c.o:
	$(CC) -MD -c $< -o $@ $(CFLAGS)

.S.o:
	$(CC) -MD -c $< -o $@ $(CFLAGS)

clean:
	rm -f init *.o *.d

install: all
	mkdir -p ../sysroot/sbin ../sysroot/bin ../sysroot/etc
	cp init ../sysroot/sbin
