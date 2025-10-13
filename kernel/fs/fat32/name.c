#include "fat_internal.h"
#include <lilac/fs.h>

void get_fat_name(char fatname[12], const struct dentry *find)
{
    int i, j;
    const char *dot = strchr(find->d_name, '.');
    memset(fatname, ' ', 11);
    fatname[11] = '\0';

    // Copy up to 8 characters for the base name, stop at a dot
    for (i = 0, j = 0; i < 8 && find->d_name[j] != '\0' &&
         find->d_name[j] != '.'; i++, j++) {
        fatname[i] = toupper(find->d_name[j]);
    }

    if (dot) {
        for (i = 8, j = 1; j <= 3 && dot[j] != '\0'; i++, j++) {
            fatname[i] = toupper(dot[j]);
        }
    }
}

void str_toupper(char *str)
{
    for (int i = 0; str[i]; i++)
        str[i] = toupper(str[i]);
}
