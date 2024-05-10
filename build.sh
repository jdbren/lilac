#!/bin/sh
set -euo pipefail

gmake install
# mkdir -p sysroot/EFI/BOOT
# mkdir -p sysroot/boot/grub
# cp resources/BOOTIA32.EFI sysroot/EFI/BOOT/BOOTIA32.EFI
# cp resources/grub.cfg sysroot/boot/grub/grub.cfg
