#!/bin/sh
set -ex
. ./iso.sh

qemu-system-$(./target-triplet-to-arch.sh $HOST) -cdrom lilac.iso -monitor stdio -m 256M -d int -d guest_errors -no-reboot
