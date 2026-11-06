/* stdlib.h - espelho mlibc adaptado ao SaguiOS. */
#ifndef MLIBC_STDLIB_H
#define MLIBC_STDLIB_H
#include "sagui.h"
void *malloc(size_t n);
void *calloc(size_t n, size_t t);
void *realloc(void *p, size_t n);
void  free(void *p);
int   atoi(const char *s);
long  atol(const char *s);
void  exit(int codigo);
void  abort(void);
int   abs(int x);
long  labs(long x);
#endif
