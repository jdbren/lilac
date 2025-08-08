#!/bin/bash
set -eux

get_ncpus() {
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        return $(nproc)
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        return $(sysctl -n hw.ncpu)
    else
        echo "Unknown host"
        exit 1
    fi
}

make -j $(get_ncpus) install-system
if [[ -d ../gush ]]; then
make -C ../gush
cp ../gush/gush ./sysroot/sbin/
fi
sudo ./scripts/image.sh

sudo qemu-system-x86_64 \
    -s -enable-kvm -no-reboot -smp 4 -m 512M \
    -machine q35,firmware=./resources/OVMF-pure-efi.fd \
    -cpu host,+tsc,+tsc-deadline,+invtsc,+rdtscp,+vmware-cpuid-freq \
    -drive file=./uefi.img,format=raw -snapshot \
    -net none \
    -monitor stdio -debugcon file:debug.txt -d int,cpu_reset,guest_errors -D log.txt
