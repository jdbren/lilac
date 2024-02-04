#!/bin/sh
set -euo pipefail
. ./image.sh

qemu-system-$(./target-triplet-to-arch.sh $HOST) -smp 4,sockets=1,cores=4 -monitor stdio -m 3G -no-reboot ./disk.img
