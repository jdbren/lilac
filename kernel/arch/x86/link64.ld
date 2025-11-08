OUTPUT_FORMAT("elf64-x86-64")

OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

KERNEL_VM_BASE = 0xffffffff80000000;

SECTIONS {
	. = 1M;

	.multiboot.text : { *(.multiboot.text) }
	.multiboot.data : { *(.multiboot.data) }

	. += KERNEL_VM_BASE;

	_kernel_start = .;

	.text ALIGN (4K) : AT (ADDR (.text) - KERNEL_VM_BASE) { *(.text .text.*) }

	.exception ALIGN (4K) : AT (ADDR (.exception) - KERNEL_VM_BASE) {
		_exception_start = .;
		*(.exception)
		_exception_end = .;
	}

	.sigtramp ALIGN (4K) : AT (ADDR (.sigtramp) - KERNEL_VM_BASE) { *(.sigtramp) }

	.rodata ALIGN (4K) : AT (ADDR (.rodata) - KERNEL_VM_BASE) { *(.rodata .rodata.*) }

	.init_array : AT (ADDR (.init_array) - KERNEL_VM_BASE) {
		PROVIDE_HIDDEN(__init_array_start = .);
		KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
		KEEP(*(.init_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .ctors))
		PROVIDE_HIDDEN(__init_array_end = .);
	}

	.data ALIGN (4K) : AT (ADDR (.data) - KERNEL_VM_BASE) {
		_data_start = .;
		*(.data)
	}

	. = ALIGN(64);
	.data.percpu : AT(ADDR(.data.percpu) - KERNEL_VM_BASE) {
		_percpu_start = .;
		*(.data.percpu.first)
		*(.data.percpu*)
		_percpu_end = .;
	}

	.bss ALIGN (4K) : AT (ADDR (.bss) - KERNEL_VM_BASE) {
		_bss_start = .;
		*(COMMON)
		*(.bss)
		*(.bsstack)
	}
	_bss_end = .;

	_kernel_end = .;

	. = ASSERT(_kernel_end < KERNEL_VM_BASE + 0x200000, "Kernel too large");

}
