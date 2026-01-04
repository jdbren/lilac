#include <stdio.h>
#include <time.h>

int main(void)
{
    time_t now;
    struct tm *tm_info;
    char buffer[128];

    time(&now);
    tm_info = localtime(&now);

    strftime(buffer, sizeof(buffer), "%a %b %e %I:%M:%S %p %Z %Y", tm_info);
    printf("%s\n", buffer);

    return 0;
}
