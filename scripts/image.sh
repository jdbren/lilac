#!/bin/sh
set -eu

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    losetup --offset 1048576 --sizelimit 46934528 /dev/loop0 uefi.img
    mount /dev/loop0 /mnt
    cp -r sysroot/* /mnt
    sync
    sleep 1
    umount /mnt
    losetup -d /dev/loop0
elif [[ "$OSTYPE" == "darwin"* ]]; then
    disk=$(hdiutil attach uefi.img)
    disk="${disk%% *}"
    cp -r sysroot/* /Volumes/LILAC
    hdiutil detach "$disk"
else
    echo "Unknown host"
    exit 1
fi

sync
