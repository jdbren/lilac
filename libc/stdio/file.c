#include <stdio.h>

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

FILE _stdin = {
    ._ptr = NULL,
    ._cnt = 0,
    ._base = NULL,
    ._file = STDIN_FILENO
};

FILE _stdout = {
    ._ptr = NULL,
    ._cnt = 0,
    ._base = NULL,
    ._file = STDOUT_FILENO
};

FILE _stderr = {
    ._ptr = NULL,
    ._cnt = 0,
    ._base = NULL,
    ._file = STDERR_FILENO
};

FILE *stdin = &_stdin;
FILE *stdout = &_stdout;
FILE *stderr = &_stderr;

FILE *fopen(const char *restrict filename, const char *restrict mode)
{
    return NULL;
}

int fclose(FILE *stream)
{
    return 0;
}

int fflush(FILE *stream)
{
    return 0;
}

int fread(void *ptr, int size, int nmemb, FILE *stream)
{
    return 0;
}

int fwrite(const void *ptr, int size, int nmemb, FILE *stream)
{
    return 0;
}
