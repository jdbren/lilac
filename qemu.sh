#!/bin/sh
set -euo pipefail
. ./image.sh

qemu-system-$(./target-triplet-to-arch.sh $HOST) -cpu max -smp 2 -monitor stdio -debugcon file:debug.txt -m 3G -no-reboot ./disk.img
