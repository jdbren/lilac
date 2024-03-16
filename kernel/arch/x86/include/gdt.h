// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef x86_GDT_H
#define x86_GDT_H

void gdt_init(void);
void set_tss_esp0(u32 esp0);

#endif
