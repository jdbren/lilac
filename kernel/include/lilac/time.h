#ifndef _LILAC_TIME_H
#define _LILAC_TIME_H

#include <lilac/types.h>
#include <lilac/config.h>
#include <lilac/math.h>

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

struct timezone {
    int tz_minuteswest; /* of Greenwich */
    int tz_dsttime;     /* type of dst correction to apply */
};

struct timespec {
    time_t tv_sec;
    s64 tv_nsec;
};

#define NSEC_PER_SEC 1'000'000'000L
#define NS_PER_SEC NSEC_PER_SEC
#define NS_PER_MS 1000000L
#define NS_PER_US 1000L

#define TIME_FORMAT "%04u/%02u/%02u %02u:%02u:%02u"

/* Parameters used to convert the timespec values: */
#define PSEC_PER_NSEC            1000L

#define TIME64_MAX ((s64)~((u64)1 << 63))
#define TIME64_MIN (-TIME64_MAX - 1)

#define KTIME_MAX     ((s64)~((u64)1 << 63))
#define KTIME_MIN     (-KTIME_MAX - 1)
#define KTIME_SEC_MAX (KTIME_MAX / NSEC_PER_SEC)
#define KTIME_SEC_MIN (KTIME_MIN / NSEC_PER_SEC)

/*
 * Limits for settimeofday():
 *
 * To prevent setting the time close to the wraparound point time setting
 * is limited so a reasonable uptime can be accomodated. Uptime of 30 years
 * should be really sufficient, which means the cutoff is 2232. At that
 * point the cutoff is just a small part of the larger problem.
 */
#define TIME_UPTIME_SEC_MAX        (30LL * 365 * 24 *3600)
#define TIME_SETTOD_SEC_MAX        (KTIME_SEC_MAX - TIME_UPTIME_SEC_MAX)

static inline int timespec_equal(const struct timespec *a,
                   const struct timespec *b)
{
    return (a->tv_sec == b->tv_sec) && (a->tv_nsec == b->tv_nsec);
}

/*
 * lhs < rhs:  return <0
 * lhs == rhs: return 0
 * lhs > rhs:  return >0
 */
static inline int timespec_compare(const struct timespec *lhs, const struct timespec *rhs)
{
    if (lhs->tv_sec < rhs->tv_sec)
        return -1;
    if (lhs->tv_sec > rhs->tv_sec)
        return 1;
    return lhs->tv_nsec - rhs->tv_nsec;
}

// extern void set_normalized_timespec(struct timespec *ts, time_t sec, s64 nsec);

// static inline struct timespec timespec_add(struct timespec lhs,
//                         struct timespec rhs)
// {
//     struct timespec ts_delta;
//     set_normalized_timespec(&ts_delta, lhs.tv_sec + rhs.tv_sec,
//                 lhs.tv_nsec + rhs.tv_nsec);
//     return ts_delta;
// }

// /*
//  * sub = lhs - rhs, in normalized form
//  */
// static inline struct timespec timespec_sub(struct timespec lhs,
//                         struct timespec rhs)
// {
//     struct timespec ts_delta;
//     set_normalized_timespec(&ts_delta, lhs.tv_sec - rhs.tv_sec,
//                 lhs.tv_nsec - rhs.tv_nsec);
//     return ts_delta;
// }

/*
 * Returns true if the timespec is norm, false if denorm:
 */
static inline bool timespec_valid(const struct timespec *ts)
{
    /* Dates before 1970 are bogus */
    if (ts->tv_sec < 0)
        return false;
    /* Can't have more nanoseconds then a second */
    if ((unsigned long)ts->tv_nsec >= NSEC_PER_SEC)
        return false;
    return true;
}

static inline bool timespec_valid_strict(const struct timespec *ts)
{
    if (!timespec_valid(ts))
        return false;
    /* Disallow values that could overflow ktime_t */
    if ((unsigned long long)ts->tv_sec >= KTIME_SEC_MAX)
        return false;
    return true;
}

static inline bool timespec_valid_settod(const struct timespec *ts)
{
    if (!timespec_valid(ts))
        return false;
    /* Disallow values which cause overflow issues vs. CLOCK_REALTIME */
    if ((unsigned long long)ts->tv_sec >= TIME_SETTOD_SEC_MAX)
        return false;
    return true;
}

/**
 * timespec_to_ns - Convert timespec to nanoseconds
 * @ts:        pointer to the timespec variable to be converted
 *
 * Returns the scalar nanosecond representation of the timespec
 * parameter.
 */
static inline s64 timespec_to_ns(const struct timespec *ts)
{
    /* Prevent multiplication overflow / underflow */
    if (ts->tv_sec >= KTIME_SEC_MAX)
        return KTIME_MAX;

    if (ts->tv_sec <= KTIME_SEC_MIN)
        return KTIME_MIN;

    return ((s64) ts->tv_sec * NSEC_PER_SEC) + ts->tv_nsec;
}

/**
 * ns_to_timespec - Convert nanoseconds to timespec
 * @nsec:    the nanoseconds value to be converted
 *
 * Returns the timespec representation of the nsec parameter.
 */
extern struct timespec ns_to_timespec(s64 nsec);

/**
 * timespec_add_ns - Adds nanoseconds to a timespec
 * @a:        pointer to timespec to be incremented
 * @ns:        unsigned nanoseconds value to be added
 */
static __always_inline void timespec_add_ns(struct timespec *a, u64 ns)
{
    a->tv_sec += __iter_div_u64_rem(a->tv_nsec + ns, NSEC_PER_SEC, &ns);
    a->tv_nsec = ns;
}

#endif
