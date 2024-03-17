#!/bin/sh
set -euo pipefail

# Remove the old configuration file
if [ -f kbuild.config ]; then
  cp kbuild.config kbuild.config.old
  rm -f kbuild.config
fi

PROJECTS="libc kernel"

MAKE="gmake -j 11"
HOST=${HOST:-$(./scripts/default-host.sh)}

AR=${HOST}-ar
AS=${HOST}-as
CC=${HOST}-gcc

PREFIX=/usr
EXEC_PREFIX=$PREFIX
BOOTDIR=/boot
LIBDIR=$EXEC_PREFIX/lib
INCLUDEDIR=$PREFIX/include

CFLAGS="-Og -std=gnu11"

SYSROOT="$(pwd)/sysroot"
CC="$CC --sysroot=$SYSROOT -isystem=$INCLUDEDIR"

echo "PROJECTS=${PROJECTS}" >> kbuild.config
echo "export MAKE=${MAKE}" >> kbuild.config
echo "export HOST=${HOST}" >> kbuild.config
echo "export AR=${AR}" >> kbuild.config
echo "export AS=${AS}" >> kbuild.config
echo "export CC=${CC}" >> kbuild.config
echo "export PREFIX=${PREFIX}" >> kbuild.config
echo "export EXEC_PREFIX=${EXEC_PREFIX}" >> kbuild.config
echo "export BOOTDIR=${BOOTDIR}" >> kbuild.config
echo "export LIBDIR=${LIBDIR}" >> kbuild.config
echo "export INCLUDEDIR=${INCLUDEDIR}" >> kbuild.config
echo "export CFLAGS=${CFLAGS}" >> kbuild.config
echo "export SYSROOT=${SYSROOT}" >> kbuild.config
