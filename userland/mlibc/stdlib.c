/* stdlib.c - heap e conversoes inteiras sem libc do hospedeiro. */
#include "internal.h"
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>

void *malloc(size_t size)
{
    void *p = aloca(size);
    if (!p) errno = ENOMEM;
    return p;
}

void *calloc(size_t count, size_t size)
{
    if (count && size > (size_t)-1 / count) {
        errno = ENOMEM;
        return NULL;
    }
    size_t total = count * size;
    void *p = aloca_zerado(total);
    if (!p) errno = ENOMEM;
    return p;
}

void *realloc(void *pointer, size_t size)
{
    if (!pointer) return malloc(size);
    if (!size) {
        libera(pointer);
        return NULL;
    }
    void *p = realoca(pointer, size);
    if (!p) errno = ENOMEM;
    return p;
}

void free(void *pointer) { libera(pointer); }

static int digito(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

static unsigned long long parse_unsigned(const char *text, char **end,
                                         int base,
                                         unsigned long long limite,
                                         int *negativo, int *estouro)
{
    const char *inicio = text;
    while (isspace((unsigned char)*text)) text++;
    int neg = 0;
    if (*text == '+' || *text == '-') {
        neg = (*text == '-');
        text++;
    }
    if (base < 0 || base == 1 || base > 36) {
        errno = EINVAL;
        if (end) *end = (char *)inicio;
        if (negativo) *negativo = 0;
        if (estouro) *estouro = 0;
        return 0;
    }

    if ((base == 0 || base == 16) && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X') && digito((unsigned char)text[2]) >= 0 &&
        digito((unsigned char)text[2]) < 16) {
        base = 16;
        text += 2;
    } else if (base == 0) {
        base = text[0] == '0' ? 8 : 10;
    }

    const char *digits = text;
    unsigned long long value = 0;
    unsigned rem;
    unsigned long long cutoff = __mlibc_divmod_u64(limite, (unsigned)base, &rem);
    unsigned cutlim = rem;
    int overflow = 0;
    for (;;) {
        int d = digito((unsigned char)*text);
        if (d < 0 || d >= base) break;
        if (value > cutoff || (value == cutoff && (unsigned)d > cutlim)) {
            overflow = 1;
        } else if (!overflow) {
            value = value * (unsigned)base + (unsigned)d;
        }
        text++;
    }
    if (text == digits) {
        if (end) *end = (char *)inicio;
        if (negativo) *negativo = 0;
        if (estouro) *estouro = 0;
        return 0;
    }
    if (end) *end = (char *)text;
    if (negativo) *negativo = neg;
    if (estouro) *estouro = overflow;
    return value;
}

long strtol(const char *text, char **end, int base)
{
    int negative, overflow;
    unsigned long long limit = (unsigned long long)LONG_MAX + 1ULL;
    unsigned long long value = parse_unsigned(text, end, base, limit,
                                              &negative, &overflow);
    if (overflow) {
        errno = ERANGE;
        return negative ? LONG_MIN : LONG_MAX;
    }
    if (negative) {
        if (value == limit) return LONG_MIN;
        return -(long)value;
    }
    if (value > (unsigned long long)LONG_MAX) {
        errno = ERANGE;
        return LONG_MAX;
    }
    return (long)value;
}

unsigned long strtoul(const char *text, char **end, int base)
{
    int negative, overflow;
    unsigned long long value = parse_unsigned(text, end, base,
                                              (unsigned long long)ULONG_MAX,
                                              &negative, &overflow);
    if (overflow) {
        errno = ERANGE;
        return ULONG_MAX;
    }
    if (negative) return 0UL - (unsigned long)value;
    return (unsigned long)value;
}

long long strtoll(const char *text, char **end, int base)
{
    int negative, overflow;
    unsigned long long limit = (unsigned long long)LLONG_MAX + 1ULL;
    unsigned long long value = parse_unsigned(text, end, base, limit,
                                              &negative, &overflow);
    if (overflow) {
        errno = ERANGE;
        return negative ? LLONG_MIN : LLONG_MAX;
    }
    if (negative) {
        if (value == limit) return LLONG_MIN;
        return -(long long)value;
    }
    if (value > (unsigned long long)LLONG_MAX) {
        errno = ERANGE;
        return LLONG_MAX;
    }
    return (long long)value;
}

unsigned long long strtoull(const char *text, char **end, int base)
{
    int negative, overflow;
    unsigned long long value = parse_unsigned(text, end, base, ULLONG_MAX,
                                              &negative, &overflow);
    if (overflow) {
        errno = ERANGE;
        return ULLONG_MAX;
    }
    return negative ? 0ULL - value : value;
}

int atoi(const char *text) { return (int)strtol(text, NULL, 10); }
long atol(const char *text) { return strtol(text, NULL, 10); }
long long atoll(const char *text) { return strtoll(text, NULL, 10); }

void exit(int status) { sair(status); }
void abort(void)
{
    (void)registrar_texto("abort() no userland");
    sair(134);
}
int abs(int value) { return value < 0 ? -value : value; }
long labs(long value) { return value < 0 ? -value : value; }
long long llabs(long long value) { return value < 0 ? -value : value; }

static unsigned int estado_rand = 1;
void srand(unsigned int seed) { estado_rand = seed; }
int rand(void)
{
    estado_rand = estado_rand * 1103515245u + 12345u;
    return (int)((estado_rand >> 16) & RAND_MAX);
}

static void trocar_bytes(unsigned char *a, unsigned char *b, size_t n)
{
    while (n--) {
        unsigned char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

static void descer_heap(unsigned char *base, size_t inicio, size_t fim,
                        size_t size, int (*compar)(const void *, const void *))
{
    size_t raiz = inicio;
    for (;;) {
        size_t esquerda = raiz * 2 + 1;
        if (esquerda > fim) break;
        size_t maior = esquerda;
        if (esquerda + 1 <= fim &&
            compar(base + esquerda * size, base + (esquerda + 1) * size) < 0)
            maior = esquerda + 1;
        if (compar(base + raiz * size, base + maior * size) >= 0) break;
        trocar_bytes(base + raiz * size, base + maior * size, size);
        raiz = maior;
    }
}

void qsort(void *base, size_t count, size_t size,
           int (*compar)(const void *, const void *))
{
    if (!base || !compar || size == 0 || count < 2) return;
    unsigned char *bytes = base;
    for (size_t i = count / 2; i-- > 0;)
        descer_heap(bytes, i, count - 1, size, compar);
    for (size_t fim = count - 1; fim > 0; fim--) {
        trocar_bytes(bytes, bytes + fim * size, size);
        descer_heap(bytes, 0, fim - 1, size, compar);
    }
}
