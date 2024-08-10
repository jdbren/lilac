#!/bin/sh
set -e

sh build.sh

disk=$(hdiutil attach uefi.img)
disk="${disk%% *}"
cp -r sysroot/* /Volumes/LILAC
hdiutil detach "$disk"
sync
