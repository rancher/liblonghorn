#ifndef LONGHORN_LOG_HEADER
#define LONGHORN_LOG_HEADER

#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdarg.h>

#define errorf(fmt, args...)					\
do {									\
	fprintf(stderr, "%s: " fmt, __FUNCTION__, ##args);		\
} while (0)

static inline void longhorn_log(const char *level, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    struct timeval tv;
    gettimeofday(&tv, NULL);

    struct tm tm_info;
    localtime_r(&tv.tv_sec, &tm_info);

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);

    fprintf(stderr, "[%s.%06ld] %s: ", buf, (long)tv.tv_usec, level);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);

    va_end(args);
}

#define LOG_ERROR(fmt, ...) \
    longhorn_log("Error", fmt, ##__VA_ARGS__)

#endif
