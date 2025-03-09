.section .init
.global _init
.type _init, @function
_init:
.code32
	push %ebp
	movl %esp, %ebp
	/* gcc will nicely put the contents of crtbegin.o's .init section here. */

.section .fini
.global _fini
.type _fini, @function
_fini:
.code32
	push %ebp
	movl %esp, %ebp
	/* gcc will nicely put the contents of crtbegin.o's .fini section here. */
