dd if=/dev/zero of=$(pwd)/uefi.img bs=512 count=93750
losetup --offset 1048576 --sizelimit 46934528 /dev/loop0 uefi.img
mkfs.fat -F 32 /dev/loop0
mount /dev/loop0 /mnt
umount /mnt
losetup -d /dev/loop0
