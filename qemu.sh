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

qemu-system-x86_64 \
    -s \
    -machine q35,firmware=./resources/OVMF-pure-efi.fd \
    -cpu max -no-reboot -smp 2 -m 512M \
    -drive file=./uefi.img,format=raw -snapshot \
    -net none \
    -monitor stdio -debugcon file:debug.txt -d int,cpu_reset,guest_errors -D log.txt

# hdiutil create -size 48m -layout GPTSPUD -ov -fs fat32 -volname NEWOS -srcfolder sysroot -format UDIF -attach uefi2.img
