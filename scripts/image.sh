#!/bin/bash
set -eu

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    LOOPDEV=$(losetup --find --show --partscan uefi.img)
    mkdir -p /mnt/lilac
    mount "${LOOPDEV}p1" /mnt/lilac
    cp -r sysroot/* /mnt/lilac
    sync
    sleep 1
    umount /mnt/lilac
    losetup -d "$LOOPDEV"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    disk=$(hdiutil attach uefi.img)
    disk="${disk%% *}"
    cp -r sysroot/* /Volumes/LILAC2
    hdiutil detach "$disk"
else
    echo "Unknown host"
    exit 1
fi

sync
