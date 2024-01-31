#!/bin/sh
set -e
. ./config.sh

mkdir -p "$SYSROOT"

for PROJECT in $SYSTEM_HEADER_PROJECTS; do
  (cd $PROJECT && DESTDIR="$SYSROOT" $MAKE install-headers)
done

mkdir -p "$SYSROOT/bin"
cp -r "$PWD/user_code/code.o" "$SYSROOT/bin"
