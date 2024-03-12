#!/bin/sh
set -euo pipefail
. ./scripts/image.sh

qemu-system-i386 -cpu max -monitor stdio -debugcon file:debug.txt \
    -m 2G -no-reboot -net none -bios ./bios32.bin -drive file=./uefi.img,format=raw \
# -d int,pcall,cpu_reset -D log.txt
