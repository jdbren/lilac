# LILAC OS

Updated 3/31/25 -
Running in long mode (x86_64) is now supported. I have ported the newlib
C library, and modified binutils and gcc sources to customize the compiler
when it may be helpful. See those ports here on the lilac-os branch:

https://github.com/jdbren/newlib/tree/lilac-os \
https://github.com/jdbren/gcc/tree/lilac-os \
https://github.com/jdbren/binutils-gdb/tree/lilac-os

------------------------------------------------------------------------------
Lilac is a hobby operating system project running on x86 (32-bit) in
early development. It is a toy project to learn about kernel programming and
operating system fundamentals.

The entry point is _start in the x86 boot folder. I am testing on the Qemu q35
emulator (for x86_64) with OVMF firmware from the EDK2 project, and I am using
a Grub2 EFI application as the bootloader.

------------------------------------------------------------------------------
## Building

### Toolchain
Install the gcc/binutils dependencies. On fedora this is
```bash
sudo dnf install gcc gcc-c++ make bison flex-devel gmp-devel libmpc-devel mpfr-devel texinfo isl-devel
```

Binutils and gcc should be built in separate directories from the source code.
You will likely need the generic elf compiler.
```bash
# Target can be x86_64-elf or i686-elf
export TARGET=x86_64-elf
```
```bash
../binutils-gdb/configure --target=$TARGET --prefix=$PREFIX --disable-nls
make
make install
```
```bash
../gcc/configure --target=$TARGET --prefix=$PREFIX --disable-nls --enable-languages=c,c++ --without-headers --disable-hosted-libstdcxx
make all-gcc
make all-target-libgcc
make all-target-libstdc++-v3
make install-gcc install-target-libgcc install-target-libstdc++-v3
```

Now onto building the custom toolchain. There is a bit of bootstrapping
since libgcc expects the system headers to be available. You have to
clone the gcc, binutils, and newlib forks and switch to the lilac-os
branch.
```bash
# x86_64-lilac or i686-lilac
export TARGET=x86_64-lilac
```
```bash
../binutils-gdb/configure --target=$TARGET --with-sysroot=$HOME/lilac/sysroot
make
make install
```
```bash
../gcc/configure --target=$TARGET --with-sysroot=$HOME/lilac/sysroot --enable-languages=c,c++ --disable-fixincludes
make all-gcc
# Need to have system headers in the sysroot include dir. Get these from newlib.
mkdir -p $HOME/lilac/sysroot/usr/local
cp -r $HOME/newlib/newlib/libc/include $HOME/lilac/sysroot/usr/local/include
make all-target-libgcc
make install-gcc install-target-libgcc
```
```bash
# Add a crt0 for compiling user space programs
x86_64-elf-gcc -o crt0.o -c ~/lilac/init/crt0.S
mv crt0.o /usr/local/lib/gcc/x86_64-lilac/16.0.0/ # or where ever the compiler is
```
```bash
# C++ support
make all-target-libstdc++-v3
make install-target-libstdc++-v3
```

### Kernel image

See scripts/create-image.sh

### Final Full Build

First run config.sh. ARCH defaults to x86_64.
You can now build and boot into a vm using qemu.sh.

ncurses
./configure --build=x86_64-linux-gnu --host=x86_64-lilac --disable-widec --without-cxx --prefix=/usr --without-manpages
make
make DESTDIR=$SYSROOT install

libedit
CFLAGS="-fcommon -std=gnu89" ./configure --build=x86_64-linux-gnu --host=x86_64-lilac --disable-widec --prefix=/usr
make
make DESTDIR=$SYSROOT install

dash
LIBS="-ledit -lncurses" ./configure --build=x86_64-linux-gnu --host=x86_64-lilac --with-libedit --prefix=/usr
make
