#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>

DIR *opendir(const char *name)
{
    int fd = open(name, 0);
    if (fd < 0)
        return NULL;
    
}
