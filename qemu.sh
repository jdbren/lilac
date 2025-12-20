#!/bin/bash
set -eu

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

make -s -j $(get_ncpus) install-system
sudo ./scripts/image.sh

qemu-system-x86_64 \
    -s -enable-kvm -no-reboot -smp 4 -m 256M \
    -machine q35,firmware=./resources/OVMF-pure-efi.fd \
    -cpu host,+tsc-deadline,+invtsc,+rdtscp,+vmware-cpuid-freq,+fsgsbase \
    -drive file=./uefi.img,format=raw -snapshot \
    -net none \
    -monitor stdio -debugcon file:debug.txt
    # -d int -D log.txt
