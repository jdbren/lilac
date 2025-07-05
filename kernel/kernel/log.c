#include <lilac/log.h>
#include <lilac/libc.h>
#include <lilac/timer.h>
#include <lilac/process.h>
#include <drivers/framebuffer.h>


#if defined DEBUG_LOG || defined DEBUG
#define LOG_LEVEL LOG_DEBUG
#elif defined WARN_LOG
#define LOG_LEVEL LOG_WARN
#elif defined ERROR_LOG
#define LOG_LEVEL LOG_ERROR
#elif defined FATAL_LOG
#define LOG_LEVEL LOG_FATAL
#else
#define LOG_LEVEL LOG_INFO
#endif

static int log_level = LOG_LEVEL;

void set_log_level(int level)
{
    log_level = level;
}

void klog(int level, const char *data, ...)
{
    u32 text_color = graphics_getcolor().fg;
    va_list args;
    if (!data || log_level > level) return;
    u64 stime = get_sys_time();
    switch (level)
    {
    case LOG_WARN:
        graphics_setcolor(RGB_YELLOW, RGB_BLACK);
        printf("[ WARNING ]");
        break;
    case LOG_ERROR:
        graphics_setcolor(RGB_RED, RGB_BLACK);
        printf("[ ERROR ]");
        break;
    case LOG_FATAL:
        graphics_setcolor(RGB_RED, RGB_BLACK);
        printf("[ PANIC ]");
        break;
    }
    graphics_setcolor(text_color, RGB_BLACK);
    printf("[%lld.%09lld] pid %d: ", (long long)(stime / 1000000000ll),
        (long long)(stime % 1000000000ll), get_pid());

    va_start(args, data);
    vprintf(data, args);
    va_end(args);
}

void kstatus(int status, const char *message, ...)
{
    va_list args;
    if (!message) return;

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
}
