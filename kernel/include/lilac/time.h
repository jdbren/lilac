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

/**
 * ktime_set - Set a ktime_t variable from a seconds/nanoseconds value
 * @secs:    seconds to set
 * @nsecs:    nanoseconds to set
 *
 * Return: The ktime_t representation of the value.
 */
static inline ktime_t ktime_set(const s64 secs, const unsigned long nsecs)
{
    if (unlikely(secs >= KTIME_SEC_MAX))
        return KTIME_MAX;

    return secs * NSEC_PER_SEC + (s64)nsecs;
}

/* Subtract two ktime_t variables. rem = lhs -rhs: */
#define ktime_sub(lhs, rhs) ((lhs) - (rhs))

/* Add two ktime_t variables. res = lhs + rhs: */
#define ktime_add(lhs, rhs) ((lhs) + (rhs))

/*
 * Same as ktime_add(), but avoids undefined behaviour on overflow; however,
 * this means that you must check the result for overflow yourself.
 */
#define ktime_add_unsafe(lhs, rhs) ((u64) (lhs) + (rhs))

/*
 * Add a ktime_t variable and a scalar nanosecond value.
 * res = kt + nsval:
 */
#define ktime_add_ns(kt, nsval) ((kt) + (nsval))

#define ktime_add_ms(kt, msval) ((kt) + (s64)(msval) * 1000000L)

static inline ktime_t ktime_add_sat(ktime_t a, ktime_t b)
{
    ktime_t res;
    if (__builtin_add_overflow(a, b, &res))
        return b > 0 ? KTIME_MAX : KTIME_MIN;
    return res;
}

/*
 * Subtract a scalar nanosecod from a ktime_t variable
 * res = kt - nsval:
 */
#define ktime_sub_ns(kt, nsval) ((kt) - (nsval))

/* convert a timespec64 to ktime_t format: */
static inline ktime_t timespec_to_ktime(struct timespec ts)
{
    return ktime_set(ts.tv_sec, ts.tv_nsec);
}

static inline ktime_t timeval_to_ktime(const struct timeval *tv)
{
    return ktime_set(tv->tv_sec, tv->tv_usec * NS_PER_US);
}

/* Map the ktime_t to timespec conversion to ns_to_timespec function */
#define ktime_to_timespec64(kt) ns_to_timespec((kt))

/* Convert ktime_t to nanoseconds */
static inline s64 ktime_to_ns(const ktime_t kt)
{
    return kt;
}

/*
 * Add two ktime values and do a safety check for overflow:
 */
static inline ktime_t ktime_add_safe(const ktime_t lhs, const ktime_t rhs)
{
    ktime_t res = ktime_add_unsafe(lhs, rhs);

    /*
     * We use KTIME_SEC_MAX here, the maximum timeout which we can
     * return to user space in a timespec:
     */
    if (res < 0 || res < lhs || res < rhs)
        res = ktime_set(KTIME_SEC_MAX, 0);

    return res;
}


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

static inline ktime_t timeval_to_ns(const struct timeval *tv)
{
    if (tv->tv_sec >= KTIME_SEC_MAX)
        return KTIME_MAX;
    return (ktime_t)tv->tv_sec * NS_PER_SEC + (ktime_t)tv->tv_usec * NS_PER_US;
}

static inline struct timeval ns_to_timeval(ktime_t ns)
{
    struct timeval tv = {
        .tv_sec = ns / NS_PER_SEC,
        .tv_usec = (ns % NS_PER_SEC + NS_PER_US - 1) / NS_PER_US,
    };
    if (tv.tv_usec == 1000000) {
        tv.tv_sec++;
        tv.tv_usec = 0;
    }
    return tv;
}

static inline struct timespec timeval_to_timespec(const struct timeval tv)
{
    struct timespec ts = {
        .tv_sec = tv.tv_sec,
        .tv_nsec = (ktime_t)tv.tv_usec * NS_PER_US,
    };
    return ts;
}

#endif
