Updated 3/31/25 - 
Running in long mode (x86_64) is now supported. I have ported the newlib
C library, and modified binutils and gcc sources to customize the compiler
when it may be helpful. See those ports here:

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
