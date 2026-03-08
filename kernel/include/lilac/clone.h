#ifndef _LILAC_CLONE_H
#define _LILAC_CLONE_H

#define CLONE_VM        0x00000100  /* VM shared between processes */
#define CLONE_FS        0x00000200  /* fs info shared between processes */
#define CLONE_FILES     0x00000400  /* file descriptors shared between processes */
#define CLONE_SIGHAND   0x00000800  /* signal handlers shared between processes */
// #define CLONE_PIDFD     0x00001000  /* set if a pidfd should be created for the child */
// #define CLONE_PTRACE    0x00002000
#define CLONE_VFORK     0x00004000  /* child is to be created with vfork semantics */
#define CLONE_PARENT    0x00008000  /* parent is shared between calling and new process */
#define CLONE_THREAD    0x00010000  /* thread group is shared between processes */
#define CLONE_NEWNS     0x00020000  /* the child is to have a new mount namespace */
#define CLONE_SYSVSEM   0x00040000  /* share the System V semaphore set */
#define CLONE_SETTLS    0x00080000  /* set TLS descriptor to tls */
#define CLONE_PARENT_SETTID 0x00100000 /* store the child thread ID at parent_tid in parent mem */
#define CLONE_CHILD_CLEARTID 0x00200000 /* clear child thread ID at child_tid on exit and wake futex*/
// #define CLONE_DETACHED  0x00400000
// #define CLONE_UNTRACED  0x00800000
#define CLONE_CHILD_SETTID 0x01000000 /* store the child thread ID at child_tid in child mem */
// #define CLONE_NEWCGROUP 0x02000000
// #define CLONE_NEWUTS    0x04000000
// #define CLONE_NEWIPC    0x08000000
// #define CLONE_NEWUSER   0x10000000
// #define CLONE_NEWPID    0x20000000
// #define CLONE_NEWNET    0x40000000
// #define CLONE_IO        0x80000000

struct clone_args {
    unsigned long flags;
    void *stack;
    pid_t *parent_tid;
    pid_t *child_tid;
    void *tls;
    int exit_signal;
};

#endif /* _LILAC_CLONE_H */
