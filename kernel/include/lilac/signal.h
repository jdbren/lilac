#ifndef _LILAC_SIGNAL_H
#define _LILAC_SIGNAL_H

/*
 * Basic signal handling related data type definitions:
 */

#include <lilac/list.h>
#include <user/signal.h>
#include <user/siginfo.h>

#define _NSIG 32

#define _NSIG_WORDS (_NSIG / (sizeof(unsigned long) * 8))

typedef unsigned long sigset_t;

typedef struct kernel_siginfo {
	__SIGINFO;
} kernel_siginfo_t;

struct sigpending {
	struct list_head list;
	sigset_t signal;
};

struct sigaction {
	__sighandler_t	sa_handler;
	unsigned long	sa_flags;
	sigset_t	    sa_mask;	/* mask last for extensibility */
};

struct ksigaction {
    struct sigaction sa;
};

struct ksignal {
	struct ksigaction ka;
	kernel_siginfo_t info;
	int sig;
};


#endif /* _LINUX_SIGNAL_TYPES_H */
