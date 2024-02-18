#!/bin/sh
set -euo pipefail
. ./image.sh

qemu-system-$(./target-triplet-to-arch.sh $HOST) -cpu max -smp 4 -monitor stdio -debugcon file:debug.txt -m 4G  -d int,pcall,cpu_reset -D log.txt -no-reboot ./disk.img
# -d int,pcall,cpu_reset -D log.txt
