#include <stdio.h>
#include <unistd.h>

int main()
{
    unsigned long i = 0;
    int pid = getpid();
    printf("CPU run test started (pid %d)\n", pid);
    while (1) {
        if (i % 1000000000 == 0) {
            printf("pid %d: i = %lu\n", pid, i);
        }
        i++;
    }
}
