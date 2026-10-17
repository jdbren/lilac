#include <lilac/fs.h>
#include <lilac/socket.h>
#include <lilac/syscall.h>

SYSCALL_DECL3(socket, int, domain, int, type, int, protocol)
{
    return -ENOSYS;
}
