.section .init
	/* gcc will nicely put the contents of crtend.o's .init section here. */
#ifdef ARCH_x86_64
	pop %rbp
#else
	pop %ebp
#endif
	ret

.section .fini
	/* gcc will nicely put the contents of crtend.o's .fini section here. */
#ifdef ARCH_x86_64
	pop %rbp
#else
	pop %ebp
#endif
	ret
