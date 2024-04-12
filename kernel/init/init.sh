i686-elf-gcc -c crt0.s -ffreestanding
i686-elf-ld -o init crt0.o main.o ../../libc/libc.a
mkdir -p ../../sysroot/bin
cp init ../../sysroot/bin/init
