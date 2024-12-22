#include <stdio.h>

int main(void)
{
    char buffer[100];

    printf("Enter a string: ");
    fgets(buffer, 100, stdin);

    printf("You entered: ");
    printf("%s", buffer);

    return 0;
}
