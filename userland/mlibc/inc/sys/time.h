/* sys/time.h - compatibilidade minima para gettimeofday. */
#ifndef SAGUI_MLIBC_SYS_TIME_H
#define SAGUI_MLIBC_SYS_TIME_H
#include <sys/types.h>

struct timeval {
    time_t      tv_sec;
    long        tv_usec;
};
struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

int gettimeofday(struct timeval *value, struct timezone *zone);

#endif
