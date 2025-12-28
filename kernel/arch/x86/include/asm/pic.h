// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef x86_PIC_H
#define x86_PIC_H

#define PIC1	        0x20		/* IO base address for master PIC */
#define PIC2		    0xA0		/* IO base address for slave PIC */
#define PIC1_COMMAND	PIC1
#define PIC1_DATA	    (PIC1+1)
#define PIC2_COMMAND	PIC2
#define PIC2_DATA	    (PIC2+1)

#define PIC_EOI		0x20		/* End-of-interrupt command code */

#ifndef __ASSEMBLY__
#include <lilac/types.h>

void pic_initialize(void);

void pic_sendEOI(u8 irq);
void pic_remap(int offset1, int offset2);

void pic_disable(void);
void IRQ_set_mask(u8 IRQline);
void IRQ_clear_mask(u8 IRQline);

u16 pic_get_irr(void);
u16 pic_get_isr(void);
#endif /* __ASSEMBLY__ */

#endif
