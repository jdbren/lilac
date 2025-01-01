#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Usage: %s\n", argv[0], " [message]");
        return 1;
    }

    printf("%s\n", argv[1]);

    return 0;
}
