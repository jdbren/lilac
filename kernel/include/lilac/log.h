#ifndef _LOG_H
#define _LOG_H

enum LOG_LEVEL {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
};

enum LOG_STATUS {
    STATUS_OK,
    STATUS_ERR,
};

int log_init(int level);
void klog(int level, const char* message, ...);
void kstatus(int status, const char* message, ...);

#endif
