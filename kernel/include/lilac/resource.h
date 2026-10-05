#ifndef _LILAC_RESOURCE_H
#define _LILAC_RESOURCE_H

#include <lilac/types.h>

#define RLIMIT_CPU        0
#define RLIMIT_FSIZE      1
#define RLIMIT_DATA       2
#define RLIMIT_STACK      3
#define RLIMIT_CORE       4
#define RLIMIT_RSS        5
#define RLIMIT_NPROC      6
#define RLIMIT_NOFILE     7
#define RLIMIT_MEMLOCK    8
#define RLIMIT_AS         9
#define RLIMIT_LOCKS      10
#define RLIMIT_SIGPENDING 11
#define RLIMIT_MSGQUEUE   12
#define RLIMIT_NICE       13
#define RLIMIT_RTPRIO     14
#define RLIMIT_RTTIME     15
#define RLIM_NLIMITS      16

#define RLIM_INFINITY (~0UL)

struct rlimit {
    u64 rlim_cur;
    u64 rlim_max;
};

struct task;

void init_rlimits(struct rlimit *rlim);
void copy_rlimits(struct rlimit *dst, struct task *leader);

#endif
