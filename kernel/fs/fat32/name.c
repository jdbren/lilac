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


time_t fat_time_to_unix(u16 date, u16 time)
{
    int year, month, day;
    int hour, min, sec;

    /* Decode FAT date */
    day   =  date        & 0x1F;
    month = (date >> 5)  & 0x0F;
    year  = (date >> 9)  & 0x7F;
    year += 1980;

    /* Decode FAT time */
    sec  = (time & 0x1F) * 2;
    min  = (time >> 5)  & 0x3F;
    hour = (time >> 11) & 0x1F;

    /* Days since Unix epoch */
    static const int days_before_month[] = {
        0,   31,  59,  90, 120, 151,
        181, 212, 243, 273, 304, 334
    };

    long days = 0;

    /* Years */
    for (int y = 1970; y < year; y++) {
        days += 365;
        if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))
            days++;
    }

    /* Months */
    days += days_before_month[month - 1];

    /* Leap day */
    if (month > 2 &&
        ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)))
        days++;

    /* Days */
    days += day - 1;

    return days * 86400L + hour * 3600L + min * 60L + sec;
}
