/* string.c - funcoes ISO C de texto sobre o heap e as rotinas da libsagui. */
#include "internal.h"
#include <string.h>
#include <stdlib.h>

void *memchr(const void *memory, int value, size_t count)
{
    const unsigned char *p = memory;
    unsigned char c = (unsigned char)value;
    for (size_t i = 0; i < count; i++)
        if (p[i] == c) return (void *)(p + i);
    return NULL;
}

size_t strlen(const char *text)
{
    const char *p = text;
    while (*p) p++;
    return (size_t)(p - text);
}

size_t strnlen(const char *text, size_t maximum)
{
    size_t n = 0;
    while (n < maximum && text[n]) n++;
    return n;
}

int strcmp(const char *left, const char *right)
{
    while (*left && (unsigned char)*left == (unsigned char)*right) {
        left++;
        right++;
    }
    return (int)(unsigned char)*left - (int)(unsigned char)*right;
}

int strncmp(const char *left, const char *right, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        unsigned char a = (unsigned char)left[i];
        unsigned char b = (unsigned char)right[i];
        if (a != b) return (int)a - (int)b;
        if (!a) return 0;
    }
    return 0;
}

char *strcpy(char *destination, const char *source)
{
    char *result = destination;
    while ((*destination++ = *source++) != 0) { }
    return result;
}

char *strncpy(char *destination, const char *source, size_t count)
{
    size_t i = 0;
    for (; i < count && source[i]; i++) destination[i] = source[i];
    for (; i < count; i++) destination[i] = '\0';
    return destination;
}

char *strcat(char *destination, const char *source)
{
    char *result = destination;
    while (*destination) destination++;
    while ((*destination++ = *source++) != 0) { }
    return result;
}

char *strncat(char *destination, const char *source, size_t count)
{
    char *result = destination;
    while (*destination) destination++;
    size_t i = 0;
    while (i < count && source[i]) {
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
    return result;
}

char *strdup(const char *text)
{
    size_t n = strlen(text);
    if (n == (size_t)-1) { errno = ENOMEM; return NULL; }
    char *copy = malloc(n + 1);
    if (!copy) return NULL;
    memcpy(copy, text, n + 1);
    return copy;
}

char *strndup(const char *text, size_t maximum)
{
    size_t n = strnlen(text, maximum);
    if (n == (size_t)-1) { errno = ENOMEM; return NULL; }
    char *copy = malloc(n + 1);
    if (!copy) return NULL;
    memcpy(copy, text, n);
    copy[n] = '\0';
    return copy;
}

char *strchr(const char *text, int character)
{
    unsigned char c = (unsigned char)character;
    for (;;) {
        if ((unsigned char)*text == c) return (char *)text;
        if (!*text) return NULL;
        text++;
    }
}

char *strrchr(const char *text, int character)
{
    unsigned char c = (unsigned char)character;
    const char *last = NULL;
    for (;;) {
        if ((unsigned char)*text == c) last = text;
        if (!*text) return (char *)last;
        text++;
    }
}

char *strstr(const char *text, const char *needle)
{
    size_t n = strlen(needle);
    if (!n) return (char *)text;
    for (; *text; text++)
        if ((unsigned char)*text == (unsigned char)*needle && !strncmp(text, needle, n))
            return (char *)text;
    return NULL;
}

static int pertence(unsigned char c, const char *set)
{
    for (; *set; set++)
        if ((unsigned char)*set == c) return 1;
    return 0;
}

char *strpbrk(const char *text, const char *accept)
{
    for (; *text; text++)
        if (pertence((unsigned char)*text, accept)) return (char *)text;
    return NULL;
}

size_t strspn(const char *text, const char *accept)
{
    size_t n = 0;
    while (text[n] && pertence((unsigned char)text[n], accept)) n++;
    return n;
}

size_t strcspn(const char *text, const char *reject)
{
    size_t n = 0;
    while (text[n] && !pertence((unsigned char)text[n], reject)) n++;
    return n;
}

char *strtok(char *text, const char *delimiters)
{
    static char *next;
    if (text) next = text;
    if (!next) return NULL;
    while (*next && pertence((unsigned char)*next, delimiters)) next++;
    if (!*next) { next = NULL; return NULL; }
    char *start = next;
    while (*next && !pertence((unsigned char)*next, delimiters)) next++;
    if (*next) *next++ = '\0';
    else next = NULL;
    return start;
}
