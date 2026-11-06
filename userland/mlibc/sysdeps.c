/* mlibc.c - camada de adaptacao do mlibc para o SaguiOS.
 *
 * O mlibc real e enorme (centenas de arquivos) e depende de syscalls Linux.
 * Aqui so o que um programa C pequeno usa de verdade, em cima da libsagui:
 * memoria (malloc sobre aloca), texto (strlen/strcpy sobre tam_texto),
 * stdio sem buffer (printf sobre imprimir) e o POSIX de arquivos sobre as
 * chamadas abrir/ler/escrever/fechar. Sem FPU: math e ponto flutuante
 * nao existem (ver docs/mlibc.md). */
#include "sagui.h"
#include "errno.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "unistd.h"
#include "fcntl.h"
#include "time.h"

int errno;

/* ---- memoria ---- */
void *malloc(unsigned int n) { return aloca(n); }
void *calloc(unsigned int n, unsigned int t)
{
    if (n && t > (unsigned int)-1 / n) { errno = ENOMEM; return NULL; }
    return aloca_zerado((size_t)(n * t));
}
void *realloc(void *p, unsigned int n) { return realoca(p, n); }
void  free(void *p) { libera(p); }

int atoi(const char *s) { return texto_para_int(s); }
long atol(const char *s) { return texto_para_int(s); }
void exit(int codigo) { sair(codigo); }
void abort(void) { sair(1); }
int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }

/* ---- texto ---- */
unsigned int strlen(const char *s) { return (unsigned int)tam_texto(s); }
int strcmp(const char *a, const char *b) { return compara_texto(a, b); }
int strncmp(const char *a, const char *b, unsigned int n) { return compara_texto_n(a, b, n); }
char *strcpy(char *d, const char *s) { return copia_texto(d, s, (unsigned int)-1); }
char *strncpy(char *d, const char *s, unsigned int n)
{
    unsigned int i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}
char *strcat(char *d, const char *s) { return junta_texto(d, s, (unsigned int)-1); }
char *strncat(char *d, const char *s, unsigned int n)
{
    unsigned int i = 0;
    while (d[i]) i++;
    unsigned int j = 0;
    while (j < n && s[j]) d[i + j++] = s[j];
    d[i + j] = 0;
    return d;
}
char *strdup(const char *s)
{
    unsigned int n = (unsigned int)tam_texto(s) + 1;
    char *p = aloca(n);
    if (p) copia_texto(p, s, n);
    return p;
}
char *strchr(const char *s, int c) { return acha_char(s, c); }
char *strrchr(const char *s, int c) { return acha_ultimo_char(s, c); }
char *strstr(const char *a, const char *b)
{
    unsigned int n = (unsigned int)tam_texto(b);
    if (!n) return (char *)a;
    for (; *a; a++)
        if (compara_texto_n(a, b, n) == 0) return (char *)a;
    return NULL;
}

/* ---- stdio sem buffer ---- */
static FILE _stdin_v  = { 0 };
static FILE _stdout_v = { 1 };
static FILE _stderr_v = { 2 };
FILE *stdin  = &_stdin_v;
FILE *stdout = &_stdout_v;
FILE *stderr = &_stderr_v;

/* Monta no buffer e escreve tudo (igual a imprimir_lista da libsagui). */
static int vform(int fd, const char *fmt, va_list ap)
{
    char buf[512];
    int n = formatar_lista(buf, sizeof buf, fmt, ap);
    if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
    return escrever_tudo(fd, buf, (unsigned)n);
}

int printf(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = vform(SAIDA, fmt, ap);
    va_end(ap);
    return n;
}
int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = vform(f->fd, fmt, ap);
    va_end(ap);
    return n;
}
int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = formatar_lista(buf, (unsigned int)-1, fmt, ap);
    va_end(ap);
    return n;
}
int snprintf(char *buf, unsigned int max, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = formatar_lista(buf, max, fmt, ap);
    va_end(ap);
    return n;
}
int puts(const char *s)
{
    int n = escrever_tudo(SAIDA, s, (unsigned int)tam_texto(s));
    escrever(SAIDA, "\n", 1);
    return n + 1;
}
int putchar(int c)
{
    char b = (char)c;
    return escrever(SAIDA, &b, 1) == 1 ? c : EOF;
}
int fputs(const char *s, FILE *f) { return escrever_tudo(f->fd, s, (unsigned int)tam_texto(s)); }
int fputc(int c, FILE *f)
{
    char b = (char)c;
    return escrever(f->fd, &b, 1) == 1 ? c : EOF;
}
FILE *fopen(const char *caminho, const char *modo)
{
    int m = ABRIR_LER;
    if (modo[0] == 'w') m = ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR;
    else if (modo[0] == 'a') m = ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_ACRESCENTAR;
    else if (modo[0] == 'r' && modo[1] == '+') m = ABRIR_LER | ABRIR_ESCREVER;
    int fd = abrir(caminho, m);
    if (fd < 0) return NULL;
    FILE *f = aloca(sizeof(FILE));
    if (!f) { fechar(fd); return NULL; }
    f->fd = fd;
    return f;
}
int fclose(FILE *f)
{
    if (!f) return EOF;
    int r = fechar(f->fd);
    libera(f);
    return r < 0 ? EOF : 0;
}
unsigned int fread(void *buf, unsigned int tam, unsigned int n, FILE *f)
{
    if (!tam) return 0;
    int l = ler(f->fd, buf, tam * n);
    return l < 0 ? 0 : (unsigned int)l / tam;
}
unsigned int fwrite(const void *buf, unsigned int tam, unsigned int n, FILE *f)
{
    if (!tam) return 0;
    int e = escrever_tudo(f->fd, buf, tam * n);
    return e < 0 ? 0 : (unsigned int)e / tam;
}
char *fgets(char *buf, int max, FILE *f)
{
    int n = ler_linha(f->fd, buf, (unsigned int)max);
    return n < 0 ? NULL : buf;
}
int fgetc(FILE *f)
{
    char c;
    return ler(f->fd, &c, 1) == 1 ? (unsigned char)c : EOF;
}
int fflush(FILE *f) { (void)f; return 0; }

/* ---- POSIX sobre abrir/ler/escrever/fechar ---- */
int close(int fd) { return fechar(fd); }
int read(int fd, void *buf, unsigned int n) { return ler(fd, buf, n); }
int write(int fd, const void *buf, unsigned int n) { return escrever(fd, buf, n); }
int getpid(void) { return meu_pid(); }
int sleep(unsigned int segundos) { dormir(segundos * 1000); return 0; }
int usleep(unsigned int usec) { dormir((usec + 999) / 1000); return 0; }
int unlink(const char *caminho) { return apagar(caminho); }
int rmdir(const char *caminho) { return remover_pasta(caminho); }
int mkdir(const char *caminho, unsigned int modo) { (void)modo; return criar_pasta(caminho); }

int open(const char *caminho, int flags, ...)
{
    int modo = ABRIR_LER;
    if (flags & O_RDWR) modo |= ABRIR_ESCREVER;
    else if (flags & O_WRONLY) modo |= ABRIR_ESCREVER;
    if (flags & O_CREAT) modo |= ABRIR_CRIAR;
    if (flags & O_TRUNC) modo |= ABRIR_TRUNCAR;
    if (flags & O_APPEND) modo |= ABRIR_ACRESCENTAR;
    int fd = abrir(caminho, modo);
    if (fd < 0) errno = EIO;
    return fd;
}
int fcntl(int fd, int cmd, ...) { (void)cmd; return fd >= 0 ? 0 : -1; }

/* ---- tempo (inteiros; sem epocha de verdade no relogio) ---- */
long time(void *p)
{
    long t = (long)(tempo_ms() / 1000);
    if (p) *(long *)p = t;
    return t;
}
long clock(void) { return (long)tempo_ms(); }

/* ---- sysdeps obrigatorios do mlibc (guia, pagina 2) ---- */
void mlibc_exit(int codigo) { sair(codigo); }
int mlibc_open(const char *c, int modo) { return abrir(c, modo); }
int mlibc_read(int fd, void *b, unsigned n) { return ler(fd, b, n); }
int mlibc_write(int fd, const void *b, unsigned n) { return escrever(fd, b, n); }
int mlibc_seek(int fd, int d, int o) { return posicionar(fd, d, o); }
int mlibc_close(int fd) { return fechar(fd); }
int mlibc_clock_get(unsigned long *ms) { *ms = tempo_ms(); return 0; }
void mlibc_libc_log(const char *t) { registrar_texto(t); }
void mlibc_libc_panic(const char *t) { registrar_texto(t); sair(1); }
void *mlibc_anon_allocate(unsigned n) { return aloca(n); }
int mlibc_anon_free(void *p, unsigned n) { (void)n; libera(p); return 0; }
int mlibc_futex_wait(void *u, int v, int ms) { (void)u; (void)v; (void)ms; return -38; }
int mlibc_futex_wake(void *u, int n) { (void)u; (void)n; return 0; }
int mlibc_vm_map(void *a, unsigned n, void **s) { (void)a; (void)n; (void)s; return -38; }
int mlibc_vm_unmap(void *a, unsigned n) { (void)a; (void)n; return -38; }
int mlibc_tcb_set(void *tcb) { (void)tcb; return 0; }
