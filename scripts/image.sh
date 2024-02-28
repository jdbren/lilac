#!/bin/sh
set -euo pipefail

gmake -s install

disk=$(hdiutil attach disk.img)
disk="${disk%% *}"
cp -r sysroot/* /Volumes/MYOS
hdiutil detach "$disk"
sync
