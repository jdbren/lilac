#include <stdio.h>
int main()
{
    unsigned long i = 0;
    while (1) {
        if (i % 1000000000 == 0) {
            printf("i = %lu\n", i);
        }
        i++;
    }
}
