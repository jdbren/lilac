#!/bin/bash
set -eu

cd init
# i686-elf-gcc -c crt0.s -ffreestanding -I../libc/include -nostdlib -lgcc
i686-elf-gcc -g -c main.c -ffreestanding -I ../libc/include -nostdlib -lgcc
i686-elf-ld -g -o init crt0.o main.o ../libc/libc.a -nostdlib
mkdir -p ../sysroot/sbin ../sysroot/bin ../sysroot/etc
cp init ../sysroot/sbin
