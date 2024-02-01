#!/bin/sh
set -euo pipefail
. ./build.sh

disk=$(hdiutil attach disk.img)
disk="${disk%% *}"
rm -r /Volumes/MYF/bin
cp -r sysroot/* /Volumes/MYF
hdiutil detach "$disk"
sync
