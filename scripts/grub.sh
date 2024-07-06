#!/bin/sh

hdiutil attach disk.img
sudo grub-install --directory=/usr/local/lib/grub/i386-pc --modules=multiboot2 --root-directory=/Volumes/MYF ./disk.img
cp grub.cfg /Volumes/MYOS/boot/grub
hdiutil detach /Volumes/MYOS

#grub-mkstandalone -O i386-efi -o BOOTIA32.EFI "boot/grub/grub.cfg=grub.cfg" --directory ~/opt/grub/lib/grub/i386-efi