#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    char *str = "Hello, world!\n";
    char *str2 = (char*)0xDEADBEEF;
    ssize_t ret = write(STDOUT_FILENO, str2, strlen(str));
    printf("ret = %d", ret);
    return ret;
}
