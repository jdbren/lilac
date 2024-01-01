#!/bin/sh
set -e
. ./build.sh

mkdir -p isodir
mkdir -p isodir/boot
mkdir -p isodir/boot/grub

cp sysroot/boot/lilac.kernel isodir/boot/lilac.kernel
cat > isodir/boot/grub/grub.cfg << EOF
menuentry "lilac" {
	multiboot /boot/lilac.kernel
}
EOF
grub-mkrescue -o lilac.iso isodir
