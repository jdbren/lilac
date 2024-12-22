#include <unistd.h>

char* getcwd(char buf[], size_t size)
{
    int result;
    asm ("int $0x80" : "=a" (result) : "a" (10), "b" (buf), "c" (size));
    return result ? buf : NULL;
}
