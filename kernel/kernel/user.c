#include <lilac/lilac.h>
#include <lilac/syscall.h>

SYSCALL_DECL0(getuid)
{
    return 0;
}

SYSCALL_DECL0(getgid)
{
    return 0;
}

SYSCALL_DECL0(geteuid)
{
    return 0;
}

SYSCALL_DECL0(getegid)
{
    return 0;
}
