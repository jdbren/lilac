#!/bin/sh
set -euo pipefail

# Remove the old configuration file
if [ -f kbuild.config ]; then
  cp kbuild.config kbuild.config.old
  rm -f kbuild.config
fi

SYSTEM_HEADER_PROJECTS="libc kernel"
PROJECTS="libc kernel"

MAKE=${MAKE:-make}
HOST=${HOST:-$(./scripts/default-host.sh)}

AR=${HOST}-ar
AS=${HOST}-as
CC=${HOST}-gcc

PREFIX=/usr
EXEC_PREFIX=$PREFIX
BOOTDIR=/boot
LIBDIR=$EXEC_PREFIX/lib
INCLUDEDIR=$PREFIX/include

CFLAGS="-Og -std=gnu11 -isystem=$INCLUDEDIR"

SYSROOT="$(pwd)/sysroot"
CC="$CC --sysroot=$SYSROOT "

# Work around that the -elf gcc targets doesn't have a system include directory
# because it was configured with --without-headers rather than --with-sysroot.
if echo "$HOST" | grep -Eq -- '-elf($|-)'; then
  CC="$CC -isystem=$INCLUDEDIR"
fi

echo "SYSTEM_HEADER_PROJECTS=libc kernel" >> kbuild.config
echo "PROJECTS=libc kernel" >> kbuild.config
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
echo "export HOST=" >> kbuild.config
