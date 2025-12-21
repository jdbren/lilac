#include "fat_internal.h"
#include <lilac/fs.h>

int fat_strcasecmp(const char *s1, const char *s2)
{
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
        if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
        if (c1 != c2) return c1 - c2;
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

void fat_get_lfn_part(struct fat_file *entry, char *buffer)
{
    struct fat_lfn *lfn = (struct fat_lfn*)entry;
    int i;
    int order = lfn->order & 0x3F;
    int offset = (order - 1) * 13;
    char *p = buffer + offset;

    // Ensure we don't overflow the 256 byte buffer
    // 13 chars per entry. Max index 255.
    if (offset + 13 >= 256)
        return;

    assert(lfn->attr == LONG_FNAME);
    assert(p + 13 < buffer + 256);

    // UCS-2 to ASCII (simple truncation)
    for (i = 0; i < 5; i++) p[i] = lfn->name1[i * 2];
    for (i = 0; i < 6; i++) p[5 + i] = lfn->name2[i * 2];
    for (i = 0; i < 2; i++) p[11 + i] = lfn->name3[i * 2];
}

void fat_get_sfn(struct fat_file *entry, char *buffer)
{
    int i, j = 0;
    for (i = 0; i < 8 && entry->name[i] != ' '; i++)
        buffer[j++] = entry->name[i];
    if (entry->ext[0] != ' ') {
        buffer[j++] = '.';
        for (i = 0; i < 3 && entry->ext[i] != ' '; i++)
            buffer[j++] = entry->ext[i];
    }
    buffer[j] = 0;
}

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
