#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>

int main()
{
    return syscall(SYS_reboot, 1);
}
