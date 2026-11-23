/* apoio_libc.c - shim mínimo de libc para as ferramentas nativas do fork.
 *
 * asagui, csagui e ldagui foram escritos com nomes familiares da libc
 * (open/read/close, printf/fprintf/snprintf, strcmp, qsort, malloc...). O
 * SaguiOS não tem libc; estas funções preenchem exatamente o que essas
 * ferramentas usam, por cima da libsagui (que já tem abrir/ler/escrever/
 * fechar/apagar, memcpy, memset e strlen). Nada aqui é um subconjunto geral:
 * cada função existe porque uma das três ferramentas a referencia.
 *
 * Lembrete honesto: o alocador é um "bump" sem liberação real (free() é
 * no-op, realloc copia). É suficiente para as ferramentas de compilação do
 * convidado; se um dia elas alocarem em laço grande, isso precisa virar um
 * alocador de verdade.
 */
#include "sagui.h"
#include <stdarg.h>

typedef struct { int fd; } FILE;
static FILE _stderr = { 2 };
static FILE _stdout = { 1 };
FILE *stderr = &_stderr;
FILE *stdout = &_stdout;

int errno = 0;

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, unsigned n)
{
    if (!n) return 0;
    while (--n && *a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

const char *strchr(const char *s, int c)
{
    char alvo = (char)c;
    while (*s) {
        if (*s == alvo) return s;
        s++;
    }
    return alvo == '\0' ? s : 0;
}

/* Formata poucos formatos: %s, %d, %i, %u, %x, %X, %c e %% (aceita %ld/%lu,
 * ignorando o "l"). Escreve em buf (até tam-1 bytes + NUL) e devolve o total
 * de bytes que seriam escritos (como o snprintf). */
static unsigned formatar(const char *fmt, va_list ap, char *buf, unsigned tam)
{
    unsigned total = 0;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            if (total + 1 < tam) buf[total] = *p;
            total++;
            continue;
        }
        p++;
        while (*p == '0' || *p == '-' || *p == '+' || *p == ' ' || *p == '#')
            p++;                              /* bandeiras */
        if (*p == '*') { (void)va_arg(ap, int); p++; }
        else while (*p >= '0' && *p <= '9') p++;   /* largura (número ou *) */
        int prec = -1;
        if (*p == '.') {
            p++;
            if (*p == '*') { prec = va_arg(ap, int); p++; }
            else {
                prec = 0;
                while (*p >= '0' && *p <= '9') {
                    prec = prec * 10 + (*p - '0');
                    p++;
                }
            }
        }
        while (*p == 'l' || *p == 'h' || *p == 'z' || *p == 'j' || *p == 't')
            p++;                              /* modificadores de tamanho */
        switch (*p) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            unsigned n = 0;
            while (s[n] && (prec < 0 || (int)n < prec)) n++;
            for (unsigned k = 0; k < n; k++) {
                if (total + 1 < tam) buf[total] = s[k];
                total++;
            }
            break;
        }
        case 'd': case 'i': {
            int v = va_arg(ap, int);
            char tmp[12];
            int k = 0;
            if (v < 0) {
                if (total + 1 < tam) buf[total] = '-';
                total++;
                v = -v;
            }
            do { tmp[k++] = (char)('0' + v % 10); v /= 10; } while (v);
            while (k--) {
                if (total + 1 < tam) buf[total] = tmp[k];
                total++;
            }
            break;
        }
        case 'u': case 'x': case 'X': {
            unsigned v = va_arg(ap, unsigned);
            const char *dig = (*p == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            unsigned base = (*p == 'u') ? 10u : 16u;
            char tmp[12];
            int k = 0;
            do { tmp[k++] = dig[v % base]; v /= base; } while (v);
            while (k--) {
                if (total + 1 < tam) buf[total] = tmp[k];
                total++;
            }
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (total + 1 < tam) buf[total] = c;
            total++;
            break;
        }
        case '%': {
            if (total + 1 < tam) buf[total] = '%';
            total++;
            break;
        }
        default:
            break;   /* formatos que não usamos: ignora */
        }
    }
    if (tam) buf[total < tam ? total : tam - 1] = '\0';
    return total;
}

static int escrever_formatado(int fd, const char *fmt, va_list ap)
{
    char tmp[512];
    unsigned n = formatar(fmt, ap, tmp, sizeof tmp);
    if (escrever(fd, tmp, n) < 0) return -1;
    return (int)n;
}

int fprintf(FILE *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = escrever_formatado(s->fd, fmt, ap);
    va_end(ap);
    return r;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = escrever_formatado(_stdout.fd, fmt, ap);
    va_end(ap);
    return r;
}

int vsnprintf(char *buf, unsigned tam, const char *fmt, va_list ap)
{
    return (int)formatar(fmt, ap, buf, tam);
}

int snprintf(char *buf, unsigned tam, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = (int)formatar(fmt, ap, buf, tam);
    va_end(ap);
    return r;
}

int puts(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    if (escrever(_stdout.fd, s, n) < 0) return -1;
    char nl = '\n';
    if (escrever(_stdout.fd, &nl, 1) < 0) return -1;
    return 0;
}

static const char *erro_texto(int e)
{
    switch (e) {
    case 1: return "operacao nao permitida";
    case 2: return "arquivo ou pasta nao existe";
    case 5: return "erro de entrada/saida";
    case 12: return "memoria insuficiente";
    case 13: return "permissao negada";
    case 22: return "argumento invalido";
    default: return "erro de sistema";
    }
}

const char *strerror(int e)
{
    return erro_texto(e);
}

long long strtoll(const char *n, char **fim, int base)
{
    long long v = 0;
    int sinal = 1;
    const char *p = n;
    if (base == 0) base = 10;
    if (*p == '-' || *p == '+') {
        if (*p == '-') sinal = -1;
        p++;
    }
    if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        p += 2;
    while (*p) {
        int d;
        if (*p >= '0' && *p <= '9') d = *p - '0';
        else if (*p >= 'a' && *p <= 'z') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z') d = *p - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
        p++;
    }
    if (fim) *fim = (char *)p;
    return sinal * v;
}

int open(const char *caminho, int modo)
{
    int m = ABRIR_LER;
    if ((modo & 3) != 0) m = ABRIR_ESCREVER;        /* O_WRONLY ou O_RDWR */
    if (modo & 0x200) m |= ABRIR_TRUNCAR;           /* O_TRUNC */
    if (modo & 0x40) m |= ABRIR_CRIAR;              /* O_CREAT */
    if (modo & 0x400) m |= ABRIR_ACRESCENTAR;       /* O_APPEND */
    return abrir(caminho, m);
}

int read(int fd, void *buf, unsigned n)
{
    return ler(fd, buf, n);
}

int write(int fd, const void *buf, unsigned n)
{
    return escrever(fd, buf, n);
}

int close(int fd)
{
    return fechar(fd);
}

int unlink(const char *caminho)
{
    return apagar(caminho);
}

/* Alocador "bump": cresce a área de dados com aumentAR_memoria e nunca
 * devolve memória (free é no-op; realloc copia para um bloco novo). */
static char *pilha_memoria;
static unsigned usados_memoria;

static void *alocar(unsigned bytes)
{
    if (!bytes) bytes = 1;
    if (!pilha_memoria || usados_memoria + bytes > 1024u * 1024u) {
        pilha_memoria = (char *)aumentar_memoria(1024u * 1024u);
        usados_memoria = 0;
        if (!pilha_memoria) return 0;
    }
    void *p = pilha_memoria + usados_memoria;
    usados_memoria += bytes;
    return p;
}

void *malloc(unsigned bytes)
{
    return alocar(bytes);
}

void *calloc(unsigned n, unsigned tam)
{
    unsigned bytes = n * tam;
    void *p = alocar(bytes);
    if (p) memset(p, 0, bytes);
    return p;
}

void free(void *p)
{
    (void)p;   /* no-op: o bump nunca devolve */
}

void *realloc(void *p, unsigned bytes)
{
    void *q = alocar(bytes);
    if (q && p && bytes) memcpy(q, p, bytes);
    return q;
}

/* Ordenação por inserção (compara e troca blocos de "tam" bytes). */
void qsort(void *base, unsigned n, unsigned tam,
           int (*cmp)(const void *, const void *))
{
    if (n <= 1) return;
    char *b = (char *)base;
    char *tmp = (char *)alocar(tam);
    if (!tmp) return;
    for (unsigned i = 1; i < n; i++) {
        memcpy(tmp, b + i * tam, tam);
        unsigned j = i;
        while (j > 0 && cmp(tmp, b + (j - 1) * tam) < 0) {
            memcpy(b + j * tam, b + (j - 1) * tam, tam);
            j--;
        }
        memcpy(b + j * tam, tmp, tam);
    }
}
