/* stdlib.h - alocacao e conversoes inteiras implementadas no SaguiOS. */
#ifndef SAGUI_MLIBC_STDLIB_H
#define SAGUI_MLIBC_STDLIB_H
#include <stddef.h>

#define RAND_MAX 32767

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
void  free(void *pointer);

int    atoi(const char *text);
long   atol(const char *text);
long long atoll(const char *text);
long   strtol(const char *text, char **end, int base);
unsigned long strtoul(const char *text, char **end, int base);
long long strtoll(const char *text, char **end, int base);
unsigned long long strtoull(const char *text, char **end, int base);
double strtod(const char *text, char **end);
float  strtof(const char *text, char **end);
long double strtold(const char *text, char **end);

void exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
int  abs(int value);
long labs(long value);
long long llabs(long long value);
void srand(unsigned int seed);
int  rand(void);

void qsort(void *base, size_t count, size_t size,
           int (*compar)(const void *, const void *));

char *getenv(const char *name);

#endif
