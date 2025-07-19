.section .init
	/* gcc will nicely put the contents of crtend.o's .init section here. */
#ifdef __x86_64__
	pop %rbp
#else
	pop %ebp
#endif
	ret

.section .fini
	/* gcc will nicely put the contents of crtend.o's .fini section here. */
#ifdef __x86_64__
	pop %rbp
#else
	pop %ebp
#endif
	ret
