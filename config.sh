#!/bin/sh
set -e

# Remove the old configuration file
if [ -f kbuild.config ]; then
  mv kbuild.config kbuild.config.old
fi

MAKE=${MAKE:-"make"}
ARCH=${ARCH:-"x86_64"}
BUILD=${BUILD:-"$(gcc -dumpmachine)"}
TARGET=${TARGET:-"$ARCH-lilac"}
HOST=$TARGET

AR=${TARGET}-ar
AS=${TARGET}-as
CC=${TARGET}-gcc
CXX=${TARGET}-g++
LD=${TARGET}-ld

CFLAGS="-g -O2 -DDEBUG"
CXXFLAGS="-g -O2"

# Define the defaults
SYSROOT="$(pwd)/sysroot"
PREFIX=/usr

EXEC_PREFIX=$PREFIX
BOOTDIR=/boot
SBINDIR=/sbin
BINDIR=$EXEC_PREFIX/bin
LIBDIR=$EXEC_PREFIX/lib
INCLUDEDIR=$PREFIX/include

# Create makefile includes
echo "export TARGET=${TARGET}" >> kbuild.config
echo "export HOST=${HOST}" >> kbuild.config
echo "export CONFIG_${ARCH}=y" >> kbuild.config

echo "export MAKE=${MAKE}" >> kbuild.config
echo "export AR=${AR}" >> kbuild.config
echo "export AS=${AS}" >> kbuild.config
echo "export CC=${CC}" >> kbuild.config
echo "export CXX=${CXX}" >> kbuild.config
echo "export LD=${LD}" >> kbuild.config
echo "export CFLAGS=${CFLAGS}" >> kbuild.config
echo "export CXXFLAGS=${CXXFLAGS}" >> kbuild.config

echo "export SYSROOT=${SYSROOT}" >> kbuild.config
echo "export PREFIX=${PREFIX}" >> kbuild.config
echo "export EXEC_PREFIX=${EXEC_PREFIX}" >> kbuild.config

echo "export BOOTDIR=${BOOTDIR}" >> kbuild.config
echo "export SBINDIR=${SBINDIR}" >> kbuild.config
echo "export BINDIR=${BINDIR}" >> kbuild.config
echo "export LIBDIR=${LIBDIR}" >> kbuild.config
echo "export INCLUDEDIR=${INCLUDEDIR}" >> kbuild.config
