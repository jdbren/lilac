#!/bin/sh
set -e
. ./scripts/image.sh

qemu-system-x86_64 -machine q35,firmware=./resources/OVMF-pure-efi.fd -m 512M \
    -cpu max -no-reboot \
    -drive file=./uefi.img,format=raw \
    -net none \
    -monitor stdio -debugcon file:debug.txt # -d int,cpu_reset,guest_errors -D log.txt

# hdiutil create -size 48m -layout GPTSPUD -ov -fs fat32 -volname NEWOS -srcfolder sysroot -format UDIF -attach uefi2.img