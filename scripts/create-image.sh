#!/bin/bash
set -eu
# Create guest disk image for Linux host

# On Mac try
# hdiutil create -size 48m -layout GPTSPUD -ov -fs fat32 -volname NEWOS -srcfolder sysroot -format UDIF -attach uefi2.img

if [[ -f "uefi.img" ]]; then
    mv uefi.img uefi.img.old
fi

dd if=/dev/zero of=uefi.img bs=512 count=937500

parted uefi.img --script mklabel gpt
parted uefi.img --script mkpart ESP fat32 1MiB 100%
parted uefi.img --script set 1 boot on

LOOPDEV=$(sudo losetup --find --show --partscan uefi.img)
sudo mkfs.fat -F32 -n LILACOS "${LOOPDEV}p1"

sudo mkdir -p /mnt/lilac
sudo mount "${LOOPDEV}p1" /mnt/lilac

mkdir -p sysroot/EFI/BOOT
mkdir -p sysroot/boot/grub
cp resources/EFI/BOOT/BOOTIA32.EFI sysroot/EFI/BOOT/BOOTIA32.EFI
cp resources/grub/grub.cfg sysroot/boot/grub/grub.cfg
sudo cp -rL sysroot/* /mnt/lilac/

sync
sudo umount /mnt/lilac
sudo losetup -d "$LOOPDEV"
