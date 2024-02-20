#!/bin/sh
set -euo pipefail

hdiutil attach disk.img
sudo grub-install --directory=/usr/local/lib/grub/i386-pc --modules=multiboot2 --root-directory=/Volumes/MYF ./disk.img
cp grub.cfg /Volumes/MYOS/boot/grub
hdiutil detach /Volumes/MYOS
