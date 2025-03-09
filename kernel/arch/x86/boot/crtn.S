.section .init
.code32
	/* gcc will nicely put the contents of crtend.o's .init section here. */
	popl %ebp
	ret

.section .fini
.code32
	/* gcc will nicely put the contents of crtend.o's .fini section here. */
	popl %ebp
	ret
