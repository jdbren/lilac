OUTPUT_FORMAT("elf64-x86-64")

OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS {
	. = 1M;

	_kernel_start = .;

	.multiboot.text : { *(.multiboot.text) }
	.multiboot.data : { *(.multiboot.data) }

	. += 0xffffffff80000000;

	.text ALIGN (4K) : AT (ADDR (.text) - 0xffffffff80000000) { *(.text) }

	.exception ALIGN (4K) : AT (ADDR (.exception) - 0xffffffff80000000) {
		_exception_start = .;
		*(.exception)
		_exception_end = .;
	}

	.rodata ALIGN (4K) : AT (ADDR (.rodata) - 0xffffffff80000000) { *(.rodata) }

	.data ALIGN (4K) : AT (ADDR (.data) - 0xffffffff80000000) {
		_data_start = .;
		*(.data)
	}

	.bss ALIGN (4K) : AT (ADDR (.bss) - 0xffffffff80000000) {
		_bss_start = .;
		*(COMMON)
		*(.bss)
		*(.bsstack)
	}
	_bss_end = .;

	_kernel_end = .;

	. = ASSERT(_kernel_end <= 0xffffffff80400000, "Kernel too large");

}
