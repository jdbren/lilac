#!/bin/sh
set -euo pipefail

gmake -s install

disk=$(hdiutil attach uefi.img)
disk="${disk%% *}"
cp -r sysroot/* /Volumes/OS
hdiutil detach "$disk"
sync
