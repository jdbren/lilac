/* Adapted from Linux Kernel */
/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _USER_LILAC_SIGNAL_H
#define _USER_LILAC_SIGNAL_H

#ifndef __ASSEMBLY__
#include <lilac/types.h>
#include <lilac/config.h>

/* Avoid too many header ordering problems.  */
struct siginfo;

#ifndef __KERNEL__
/* Here we must cater to libcs that poke about in kernel headers.  */

#define NSIG		32
typedef unsigned long sigset_t;

#endif /* !__KERNEL__ */
#endif /* !__ASSEMBLY__ */


#define SIGHUP		 1
#define SIGINT		 2
#define SIGQUIT		 3
#define SIGILL		 4
#define SIGTRAP		 5
#define SIGABRT		 6
#define SIGIOT		 6
#define SIGBUS		 7
#define SIGFPE		 8
#define SIGKILL		 9
#define SIGUSR1		10
#define SIGSEGV		11
#define SIGUSR2		12
#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15
#define SIGSTKFLT	16
#define SIGCHLD		17
#define SIGCONT		18
#define SIGSTOP		19
#define SIGTSTP		20
#define SIGTTIN		21
#define SIGTTOU		22
#define SIGURG		23
#define SIGXCPU		24
#define SIGXFSZ		25
#define SIGVTALRM	26
#define SIGPROF		27
#define SIGWINCH	28
#define SIGIO		29
#define SIGPOLL		SIGIO
/*
#define SIGLOST		29
*/
#define SIGPWR		30
#define SIGSYS		31
#define	SIGUNUSED	31


#ifndef __ASSEMBLY__

#include <lilac/signal-defs.h>

#ifndef __KERNEL__
/* Here we must cater to libcs that poke about in kernel headers.  */

#ifdef __x86_64__

struct sigaction {
	__sighandler_t sa_handler;
	unsigned long sa_flags;
	__sigrestore_t sa_restorer;
	sigset_t sa_mask;		/* mask last for extensibility */
};

#else // i386:

struct sigaction {
	union {
	  __sighandler_t _sa_handler;
	  void (*_sa_sigaction)(int, struct siginfo *, void *);
	} _u;
	sigset_t sa_mask;
	unsigned long sa_flags;
	void (*sa_restorer)(void);
};

#define sa_handler	    _u._sa_handler
#define sa_sigaction    _u._sa_sigaction

#endif /* !__i386__ */
#endif /* !__KERNEL__ */

typedef struct sigaltstack {
	void __user *ss_sp;
	int ss_flags;
	size_t ss_size;
} stack_t;

#endif /* __ASSEMBLER__ */

#endif /* _USER_LILAC_SIGNAL_H */
