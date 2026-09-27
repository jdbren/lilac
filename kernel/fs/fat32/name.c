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

    if (order == 0 || offset + 13 >= FAT_LFN_BUF)
        return;

    assert(lfn->attr == LONG_FNAME);

    // UCS-2 to ASCII (simple truncation)
    for (i = 0; i < 5; i++) p[i] = lfn->name1[i * 2];
    for (i = 0; i < 6; i++) p[5 + i] = lfn->name2[i * 2];
    for (i = 0; i < 2; i++) p[11 + i] = lfn->name3[i * 2];
}

// NT/Linux store "all lowercase" for the base and extension of a short
// name in the reserved byte
#define FAT_NT_LOWER_BASE 0x08
#define FAT_NT_LOWER_EXT  0x10

void fat_get_sfn(struct fat_file *entry, char *buffer)
{
    int i, j = 0;
    bool lower_base = entry->reserved & FAT_NT_LOWER_BASE;
    bool lower_ext = entry->reserved & FAT_NT_LOWER_EXT;
    for (i = 0; i < 8 && entry->name[i] != ' '; i++)
        buffer[j++] = lower_base ? tolower(entry->name[i]) : entry->name[i];
    if (entry->ext[0] != ' ') {
        buffer[j++] = '.';
        for (i = 0; i < 3 && entry->ext[i] != ' '; i++)
            buffer[j++] = lower_ext ? tolower(entry->ext[i]) : entry->ext[i];
    }
    buffer[j] = 0;
}

// Build the 11-byte 8.3 name (space padded, upper case) for name, split at
// the last dot. Returns the NT case flags for the reserved byte.
u8 fat_make_sfn(const char *name, unsigned char sfn[11])
{
    const char *dot = strrchr(name, '.');
    if (dot == name)
        dot = NULL;
    size_t base_len = dot ? (size_t)(dot - name) : strlen(name);
    bool base_lower = false, base_upper = false, ext_lower = false, ext_upper = false;

    memset(sfn, ' ', 11);
    for (size_t i = 0, j = 0; i < base_len && j < 8; i++) {
        char c = name[i];
        if (c == ' ' || c == '.')
            continue;
        base_lower |= islower(c);
        base_upper |= isupper(c);
        sfn[j++] = toupper(c);
    }
    if (dot) {
        for (size_t i = 1, j = 8; dot[i] && j < 11; i++, j++) {
            ext_lower |= islower(dot[i]);
            ext_upper |= isupper(dot[i]);
            sfn[j] = toupper(dot[i]);
        }
    }

    u8 flags = 0;
    if (base_lower && !base_upper)
        flags |= FAT_NT_LOWER_BASE;
    if (ext_lower && !ext_upper)
        flags |= FAT_NT_LOWER_EXT;
    return flags;
}

void get_fat_name(char fatname[12], const struct dentry *find)
{
    int i, j;
    const char *d_name = find->d_name.data;
    const char *dot = strchr(d_name, '.');
    memset(fatname, ' ', 11);
    fatname[11] = '\0';

    // Copy up to 8 characters for the base name, stop at a dot
    for (i = 0, j = 0; i < 8 && d_name[j] != '\0' &&
         d_name[j] != '.'; i++, j++) {
        fatname[i] = toupper(d_name[j]);
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
