#!/bin/sh
set -euo pipefail
. ./image.sh

qemu-system-$(./target-triplet-to-arch.sh $HOST) -monitor stdio -m 3G -no-reboot ./disk.img
