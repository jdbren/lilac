#include <stdio.h>
#include <unistd.h>

int main()
{
    printf("Starting init task\n");
    fork();
}
