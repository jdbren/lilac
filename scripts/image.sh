#!/bin/sh

sh build.sh

disk=$(hdiutil attach uefi.img)
disk="${disk%% *}"
cp -r sysroot/* /Volumes/OS
hdiutil detach "$disk"
sync
