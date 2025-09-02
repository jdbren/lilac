#ifndef _LILAC_TIME_H
#define _LILAC_TIME_H

#include <lilac/types.h>

struct timeval {
    time_t tv_sec;
    suseconds_t tv_usec;
};

struct tm {
    int         tm_sec;    /* Seconds          [0, 60] */
    int         tm_min;    /* Minutes          [0, 59] */
    int         tm_hour;   /* Hour             [0, 23] */
    int         tm_mday;   /* Day of the month [1, 31] */
    int         tm_mon;    /* Month            [0, 11]  (January = 0) */
    int         tm_year;   /* Year minus 1900 */
    int         tm_wday;   /* Day of the week  [0, 6]   (Sunday = 0) */
    int         tm_yday;   /* Day of the year  [0, 365] (Jan/01 = 0) */
    int         tm_isdst;  /* Daylight savings flag */

    long        tm_gmtoff; /* Seconds East of UTC */
    const char *tm_zone;   /* Timezone abbreviation */
};

#endif
