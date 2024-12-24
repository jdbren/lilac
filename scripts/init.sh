#!/bin/bash
set -eux

cd init
i686-elf-gcc -c main.c -ffreestanding -I ../libc/include -nostdlib -lgcc
i686-elf-ld -o init crt0.o main.o ../libc/libc.a -nostdlib
mkdir -p ../sysroot/sbin ../sysroot/bin ../sysroot/etc
cp init ../sysroot/sbin

cd ../user
i686-elf-gcc -c ls.c -ffreestanding -I ../libc/include -nostdlib -lgcc
i686-elf-ld -o ls ../init/crt0.o ls.o ../libc/libc.a -nostdlib
cp ls ../sysroot/bin
