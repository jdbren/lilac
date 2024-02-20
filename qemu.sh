#!/bin/sh
set -euo pipefail
. ./scripts/image.sh

qemu-system-i386 -cpu max -smp 4 -monitor stdio -debugcon file:debug.txt -m 4G -no-reboot ./disk.img
# -d int,pcall,cpu_reset -D log.txt
