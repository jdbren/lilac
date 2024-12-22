#include <stdlib.h>
#include <unistd.h>

void _Exit(int status) {
    _exit(status);
}

// TODO: Add libc cleanup code here
void exit(int status) {
    _exit(status);
}
