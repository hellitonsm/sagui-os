/* time.h - relogio monotonic e calendario RTC do SaguiOS. */
#ifndef SAGUI_MLIBC_TIME_H
#define SAGUI_MLIBC_TIME_H
#include <sys/types.h>

typedef long clock_t;
typedef int clockid_t;
struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

#define CLOCKS_PER_SEC 1000L
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

clock_t clock(void);
time_t  time(time_t *result);
int     clock_gettime(clockid_t clock_id, struct timespec *result);
int     nanosleep(const struct timespec *request, struct timespec *remaining);

struct tm {
    int tm_sec;    /* 0-59 */
    int tm_min;    /* 0-59 */
    int tm_hour;   /* 0-23 */
    int tm_mday;   /* 1-31 */
    int tm_mon;    /* 0-11 */
    int tm_year;   /* anos desde 1900 */
    int tm_wday;   /* 0 = domingo */
    int tm_yday;   /* dias desde 1 de janeiro */
    int tm_isdst;  /* sempre 0 */
};

struct tm *localtime(const time_t *tempo);

#endif
