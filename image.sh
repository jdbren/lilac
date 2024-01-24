#!/bin/sh
set -euo pipefail
. ./build.sh

disk=$(hdiutil attach disk.img)
disk="${disk%% *}"
cp -r sysroot/* /Volumes/OS
hdiutil detach "$disk"
sync
