#!/bin/sh
set -euo pipefail
. ./image.sh

qemu-system-$(./target-triplet-to-arch.sh $HOST) -cpu max -smp 4 -monitor stdio -debugcon file:debug.txt -d int,pcall,cpu_reset -D log.txt -m 3G -no-reboot ./disk.img
