ENTRY (_start)

SECTIONS
{
    . = 1M;
	/* The kernel will live at 3GB + 1MB in the virtual address space, */
	/* which will be mapped to 1MB in the physical address space. */
	/* Note that we page-align the sections. */

	_kernel_start = .;

	.multiboot.data : { *(.multiboot.data) }
	.multiboot.text : { *(.multiboot.text) }

	. += 0xC0000000;

	.text ALIGN (4K) : AT (ADDR (.text) - 0xC0000000) { *(.text) }

	.rodata ALIGN (4K) : AT (ADDR (.rodata) - 0xC0000000) { *(.rodata) }

	.data ALIGN (4K) : AT (ADDR (.data) - 0xC0000000) {
		_data_start = .;
		*(.data)
	}

	.bss ALIGN (4K) : AT (ADDR (.bss) - 0xC0000000) {
		_bss_start = .;
		*(COMMON)
		*(.bss)
		*(.bsstack)
	}
	_bss_end = .;

	_kernel_end = .;

	. = ASSERT(_kernel_end <= 0xC0400000, "Kernel too large");
}
