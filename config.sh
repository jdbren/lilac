#!/bin/sh
set -e

# Remove the old configuration file
if [ -f kbuild.config ]; then
  mv kbuild.config kbuild.config.old
fi

PROJECTS="kernel init user"

MAKE="make"
ARCH=${ARCH:-"x86_64"}
HOST=${HOST:-"$ARCH-elf"}
TARGET=${TARGET:-"$ARCH-lilac"}

AR=${HOST}-ar
AS=${HOST}-as
CC=${HOST}-gcc
CXX=${HOST}-g++

PREFIX=/usr/local
EXEC_PREFIX=$PREFIX
BOOTDIR=/boot
LIBDIR=$EXEC_PREFIX/lib
INCLUDEDIR=$PREFIX/include

CFLAGS="-g -DDEBUG"
CXXFLAGS="-g"

SYSROOT="$(pwd)/sysroot"
CC="$CC --sysroot=$SYSROOT"
CXX="$CXX --sysroot=$SYSROOT"

echo "export PROJECTS=${PROJECTS}" >> kbuild.config
echo "export MAKE=${MAKE}" >> kbuild.config
echo "export HOST=${HOST}" >> kbuild.config
echo "export AR=${AR}" >> kbuild.config
echo "export AS=${AS}" >> kbuild.config
echo "export CC=${CC}" >> kbuild.config
echo "export CXX=${CXX}" >> kbuild.config
echo "export LD=${HOST}-ld" >> kbuild.config
echo "export CONFIG_${HOST}=y" >> kbuild.config
echo "export PREFIX=${PREFIX}" >> kbuild.config
echo "export EXEC_PREFIX=${EXEC_PREFIX}" >> kbuild.config
echo "export BOOTDIR=${BOOTDIR}" >> kbuild.config
echo "export LIBDIR=${LIBDIR}" >> kbuild.config
echo "export INCLUDEDIR=${INCLUDEDIR}" >> kbuild.config
echo "export CFLAGS=${CFLAGS}" >> kbuild.config
echo "export CXXFLAGS=${CXXFLAGS}" >> kbuild.config
echo "export SYSROOT=${SYSROOT}" >> kbuild.config
echo "export TARGET=${TARGET}" >> kbuild.config
