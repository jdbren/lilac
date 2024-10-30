#ifndef _LILAC_TIMER_H
#define _LILAC_TIMER_H

#include <lilac/types.h>

struct timestamp {
    u16 year;
    u8 month;
    u8 day;
    u8 hour;
    u8 minute;
    u8 second;
};

void sleep(u32 ms);
u64 get_sys_time(void);
s64 get_unix_time(void);
struct timestamp get_timestamp(void);

#define TIME_FORMAT "%04u/%02u/%02u %02u:%02u:%02u"

#endif
