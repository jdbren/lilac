#!/bin/sh
set -euo pipefail

gmake install
mkdir -p sysroot/EFI/BOOT
cp BOOTIA32.EFI sysroot/EFI/BOOT/BOOTIA32.EFI
cp grub.cfg sysroot/boot/grub/grub.cfg
