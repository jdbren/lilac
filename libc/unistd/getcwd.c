#include <unistd.h>
#include <sys/syscall.h>

char* getcwd(char buf[], size_t size)
{
    return (char*)syscall2(SYS_getcwd, (long)buf, size);
}
