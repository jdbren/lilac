#include <fcntl.h>
int main(void)
{
    char *test = (char*)0x12345678;
    return open(test, 0);
}