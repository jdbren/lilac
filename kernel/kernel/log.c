#include <lilac/tty.h>
#include <lilac/log.h>
#include <stdio.h>
#include <stdarg.h>


#if defined DEBUG_LOG
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

static int log_level = LOG_DEBUG;

int log_init(int level)
{
    log_level = level;
    return 0;
}

void klog(int level, const char *data, ...)
{
    va_list args;
    if (!data || log_level > level) return;
    putchar('[');
    switch (level)
    {
    case LOG_DEBUG:
        graphics_setcolor(RGB_MAGENTA, RGB_BLACK);
        printf(" DEBUG ");
        break;
    case LOG_INFO:
        graphics_setcolor(RGB_CYAN, RGB_BLACK);
        printf(" INFO ");
        break;
    case LOG_WARN:
        graphics_setcolor(RGB_YELLOW, RGB_BLACK);
        printf(" WARNING ");
        break;
    case LOG_ERROR:
        graphics_setcolor(RGB_RED, RGB_BLACK);
        printf(" ERROR ");
        break;
    case LOG_FATAL:
        graphics_setcolor(RGB_RED, RGB_BLACK);
        printf(" FATAL ");
        break;
    }
    graphics_setcolor(RGB_LIGHT_GRAY, RGB_BLACK);
    printf("] ");

    va_start(args, level);
    vprintf(data, args);
    va_end(args);
}

void kstatus(int status, const char *message, ...)
{
    va_list args;
    if (!message) return;

    putchar('[');
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
    graphics_setcolor(RGB_LIGHT_GRAY, RGB_BLACK);
    printf("] ");

    va_start(args, message);
    vprintf(message, args);
    va_end(args);
}
