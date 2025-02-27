#include <stdlib.h>
#include <unistd.h>

void _Exit(int status) {
    _exit(status);
    __builtin_unreachable();
}

// TODO: Add libc cleanup code here
void exit(int status) {
    _exit(status);
    __builtin_unreachable();
}
