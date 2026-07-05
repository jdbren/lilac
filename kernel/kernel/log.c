#include <lilac/log.h>
#include <lilac/libc.h>
#include <lilac/timer.h>
#include <lilac/process.h>
#include <lilac/console.h>
#include <drivers/framebuffer.h>


#if defined DEBUG_LOG || defined DEBUG
#define LOG_LEVEL LOG_DEBUG
#elif defined WARN_LOG
#define LOG_LEVEL LOG_WARN
#elif defined ERROR_LOG
#define LOG_LEVEL LOG_ERROR
#else
#define LOG_LEVEL LOG_INFO
#endif

static int log_level = LOG_LEVEL;
static spinlock_t log_lock = SPINLOCK_INIT;

void set_log_level(int level)
{
    log_level = level;
}

void klog_lock(void)
{
    acquire_lock(&log_lock);
}

void klog_unlock(void)
{
    release_lock(&log_lock);
}

void kvlog_raw_nolock(const char *data, va_list args)
{
    vprintf(data, args);
}

void klog_raw_nolock(const char *data, ...)
{
    va_list args;
    va_start(args, data);
    vprintf(data, args);
    va_end(args);
}

void kvlog_raw(const char *data, va_list args)
{
    acquire_lock(&log_lock);
    vprintf(data, args);
    release_lock(&log_lock);
}

void kvlog(int level, const char *data, va_list args)
{
    if (!data || log_level > level) return;

    int orig_write_to_screen = write_to_screen;
    long long stime = (long long) get_sys_time_ns();

    acquire_lock(&log_lock);
    struct framebuffer_color text_color = graphics_getcolor();

    if (level == LOG_ERROR)
        write_to_screen = 1;

    printf(LOG_PREFIX, stime / 1000000000ll, stime % 1000000000ll, get_pid());

    switch (level) {
    case LOG_WARN:
        graphics_setcolor(RGB_YELLOW, RGB_BLACK);
        printf(KERN_WARN);
        break;
    case LOG_ERROR:
        graphics_setcolor(RGB_RED, RGB_BLACK);
        printf(KERN_ERR);
        break;
    }
    graphics_setcolor(text_color.fg, text_color.bg);

    vprintf(data, args);

    write_to_screen = orig_write_to_screen;
    release_lock(&log_lock);
}

void klog(int level, const char *data, ...)
{
    va_list args;
    va_start(args, data);
    kvlog(level, data, args);
    va_end(args);
}

void kstatus(int status, const char *message, ...)
{
    va_list args;
    if (!message) return;

    acquire_lock(&log_lock);

    printf("[");
    switch (status)
    {
    case STATUS_OK:
        graphics_setcolor(RGB_GREEN, RGB_BLACK);
        printf(" OK ");
        break;
    case STATUS_ERR:
        graphics_setcolor(RGB_RED, RGB_BLACK);
        printf(" ERROR ");
        break;
    }
    graphics_setcolor(RGB_WHITE, RGB_BLACK);
    printf("] ");

    va_start(args, message);
    vprintf(message, args);
    va_end(args);

    release_lock(&log_lock);
}
