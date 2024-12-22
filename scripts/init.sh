#!/bin/bash
set -eux

cd init
i686-elf-gcc -c main.c -ffreestanding -I ../libc/include -nostdlib -lgcc
i686-elf-ld -o init crt0.o main.o ../libc/libc.a -nostdlib
mkdir -p ../sysroot/sbin ../sysroot/bin ../sysroot/etc
cp init ../sysroot/sbin

# cd ../user
# i686-elf-gcc -c echo.c -ffreestanding -I ../libc/include -nostdlib -lgcc
# i686-elf-ld -o echo ../init/crt0.o echo.o ../libc/libc.a -nostdlib
# cp echo ../sysroot/bin
