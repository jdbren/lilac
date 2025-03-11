.section .init
.global _init
.type _init, @function
_init:
#ifdef ARCH_x86_64
	push %rbp
	mov %rsp, %rbp
#else
	push %ebp
	mov %esp, %ebp
#endif
	/* gcc will nicely put the contents of crtbegin.o's .init section here. */

.section .fini
.global _fini
.type _fini, @function
_fini:
#ifdef ARCH_x86_64
	push %rbp
	mov %rsp, %rbp
#else
	push %ebp
	mov %esp, %ebp
#endif
	/* gcc will nicely put the contents of crtbegin.o's .fini section here. */
