ENTRY (_start)

SECTIONS
{
    . = 1M;

	_kernel_start = .;

	.multiboot.text : { *(.multiboot.text) }
	.multiboot.data : { *(.multiboot.data) }

	. += 0xC0000000;

	.text ALIGN (4K) : AT (ADDR (.text) - 0xC0000000) { *(.text .text.*) }

	.exception ALIGN (4K) : AT (ADDR (.exception) - 0xC0000000) {
		_exception_start = .;
		*(.exception)
		_exception_end = .;
	}

	.sigtramp ALIGN (4K) : AT (ADDR (.sigtramp) - 0xC0000000) {
		*(.sigtramp)
	}

	.rodata ALIGN (4K) : AT (ADDR (.rodata) - 0xC0000000) { *(.rodata .rodata.*) }

	.init_array : AT (ADDR (.init_array) - 0xC0000000) {
		PROVIDE_HIDDEN(__init_array_start = .);
		KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
		KEEP(*(.init_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .ctors))
		PROVIDE_HIDDEN(__init_array_end = .);
	}

	.data ALIGN (4K) : AT (ADDR (.data) - 0xC0000000) {
		_data_start = .;
		*(.data)
	}

	. = ALIGN(64);
	.data.percpu : AT(ADDR(.data.percpu) - 0xC0000000) {
		_percpu_start = .;
		*(.data.percpu.first)
		*(.data.percpu*)
		_percpu_end = .;
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
