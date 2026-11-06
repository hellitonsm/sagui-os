/* string.h - espelho mlibc adaptado ao SaguiOS.
 * O mlibc tem memcpy/memmove/memset/memcmp/strlen/strcmp/strcpy/strncpy/
 * strcat/strncat/strdup/strchr/strrchr/strstr. Os cinco primeiros cheios ja
 * vivem na libsagui; o resto e adaptado em mlibc.c. */
#ifndef MLIBC_STRING_H
#define MLIBC_STRING_H
#include "sagui.h"
/* ja declarados em sagui.h: memcpy memmove memset memcmp */
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strcpy(char *d, const char *s);
char  *strncpy(char *d, const char *s, size_t n);
char  *strcat(char *d, const char *s);
char  *strncat(char *d, const char *s, size_t n);
char  *strdup(const char *s);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *a, const char *b);
#endif
