CC=$(TARGET)-gcc
CFLAGS?=-g

.PHONY: all clean install
.SUFFIXES: .o .c .S
all: init

init: main.c
	$(CC) -o $@ main.c $(CFLAGS)

clean:
	rm -f init *.o *.d

install: all
	mkdir -p ../sysroot/sbin ../sysroot/bin ../sysroot/etc
	cp init ../sysroot/sbin
