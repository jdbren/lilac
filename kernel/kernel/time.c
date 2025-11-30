#include <lilac/time.h>
#include <lilac/timer.h>
#include <lilac/syscall.h>


SYSCALL_DECL2(gettimeofday, struct timeval*, tv, struct timezone*, tz)
{
    extern s64 boot_unix_time;
    if (tv) {
        u64 sys_time = get_sys_time();
        tv->tv_sec = boot_unix_time + sys_time / 1000000000;
        tv->tv_usec = sys_time / 1000 % 1000000;
    }
    if (tz) {
        // Timezone not supported
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }
    return 0;
}
