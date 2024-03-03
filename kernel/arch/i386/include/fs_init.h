// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef x86_FS_INIT_H
#define x86_FS_INIT_H

#include <utility/multiboot2.h>

void fs_init(struct multiboot_tag_bootdev* boot_dev);

#endif // FS_INIT_H
