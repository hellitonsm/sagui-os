/* time.h - espelho mlibc adaptado ao SaguiOS (inteiros, sem FPU). */
#ifndef MLIBC_TIME_H
#define MLIBC_TIME_H
#include "sagui.h"
typedef long time_t;
typedef long clock_t;
#define CLOCKS_PER_SEC 1000
time_t time(void *p);
clock_t clock(void);
int    clock_gettime(int id, void *ts);
#endif
