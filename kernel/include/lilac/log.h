#ifndef _LOG_H
#define _LOG_H

#include <stdarg.h>

enum LOG_LEVEL {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
};

enum LOG_STATUS {
    STATUS_OK,
    STATUS_ERR,
};

#define LOG_PREFIX "[%4lld.%09lld] (%d) "
#define KERN_DEBUG LOG_PREFIX
#define KERN_INFO  LOG_PREFIX
#define KERN_WARN  LOG_PREFIX "WARNING: "
#define KERN_ERR   LOG_PREFIX "ERROR: "

void set_log_level(int level);
void klog(int level, const char* message, ...);
void kvlog(int level, const char* message, va_list args);
void kstatus(int status, const char* message, ...);

#endif
