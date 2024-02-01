#!/bin/sh
set -euo pipefail

hdiutil attach disk.img
sudo grub-install --directory=/usr/local/lib/grub/i386-pc --root-directory=/Volumes/MYF ./disk.img
cp grub.cfg /Volumes/MYF/boot/grub
hdiutil detach /Volumes/MYF
