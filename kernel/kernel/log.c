#include <lilac/log.h>
#include <lilac/libc.h>
#include <lilac/timer.h>
#include <lilac/process.h>
#include <lilac/console.h>
#include <lilac/panic.h>
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
spinlock_t log_lock = SPINLOCK_INIT;

void set_log_level(int level)
{
    log_level = level;
}

void klog(int level, const char *data, ...)
{
    int orig_write_to_screen = write_to_screen;
    acquire_lock(&log_lock);
    u32 text_color = graphics_getcolor().fg;
    va_list args;
    if (!data || log_level > level) return;
    if (level == LOG_ERROR)
        write_to_screen = 1;
    u64 stime = get_sys_time();
    printf("[%4lld.%09lld] (pid: %d) ", (long long)(stime / 1000000000ll),
        (long long)(stime % 1000000000ll), get_pid());
    switch (level)
    {
    case LOG_WARN:
        graphics_setcolor(RGB_YELLOW, RGB_BLACK);
        break;
    case LOG_ERROR:
        graphics_setcolor(RGB_RED, RGB_BLACK);
        printf("ERROR: ");
        break;
    }
    graphics_setcolor(text_color, RGB_BLACK);

    va_start(args, data);
    vprintf(data, args);
    va_end(args);

    write_to_screen = orig_write_to_screen;
    release_lock(&log_lock);
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

__noreturn void kerror(const char *msg, ...)
{
    va_list args;
	write_to_screen = 1;
    u64 stime = get_sys_time();

    printf("[%4lld.%09lld] ", (long long)(stime / 1000000000ll),
        (long long)(stime % 1000000000ll));
    putchar('[');
	graphics_setcolor(RGB_RED, RGB_BLACK);
	printf(" PANIC ");
    graphics_setcolor(RGB_WHITE, RGB_BLACK);
    putchar(']');
    printf(" (pid %d) ", get_pid());

	va_start(args, msg);
	vprintf(msg, args);
	va_end(args);

	asm("cli");
	while (1)
		asm("hlt");
}
