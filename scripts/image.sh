#!/bin/sh
set -euo pipefail

make install

disk=$(hdiutil attach disk.img)
disk="${disk%% *}"
cp -r sysroot/* /Volumes/MYOS
hdiutil detach "$disk"
sync
