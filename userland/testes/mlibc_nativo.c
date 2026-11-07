/* Teste de contrato POSIX da shim mlibc do SaguiOS.
 * Nao usa o mlibc upstream: valida headers, libc minima e traducao de errno. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <mlibc/sysdeps.h>

#define FALHA(codigo, texto) do { fprintf(stderr, "[MLIBC] falhou: %s\n", texto); return (codigo); } while (0)

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;

    /* Heap, overflow de calloc e semantica basica de string. */
    char *p = malloc(32);
    if (!p) FALHA(1, "malloc");
    strcpy(p, "sagui");
    if (strcmp(p, "sagui") || strlen(p) != 5) FALHA(2, "string basica");
    if (strrchr(p, '\0') != p + 5 || strchr(p, 'z') != NULL) FALHA(3, "strchr/strrchr");
    p = realloc(p, 64);
    if (!p || strcmp(p, "sagui")) FALHA(4, "realloc");
    free(p);
    unsigned char *zero = calloc(8, 4);
    if (!zero) FALHA(5, "calloc");
    for (int i = 0; i < 32; i++) if (zero[i]) FALHA(6, "calloc nao zerou");
    free(zero);
    errno = 0;
    if (calloc((size_t)-1, 2) || errno != ENOMEM) FALHA(7, "overflow calloc");

    char *fim = NULL;
    if (strtol(" -0x2aZ", &fim, 0) != -42 || !fim || *fim != 'Z')
        FALHA(8, "strtol base automatica");
    if (strtoul("-1", NULL, 10) != ULONG_MAX) FALHA(9, "strtoul negativo");
    if (strtoll("-9223372036854775808", &fim, 10) != LLONG_MIN || !fim || *fim)
        FALHA(46, "strtoll limite minimo");
    errno = 0;
    if (strtol("99999999999999999999", NULL, 10) != LONG_MAX || errno != ERANGE)
        FALHA(47, "strtol overflow");
    char *duplicada = strdup("shim");
    if (!duplicada || strcmp(duplicada, "shim")) FALHA(48, "strdup");
    free(duplicada);

    /* printf C99: flags, zero padding, tamanho e truncamento com NUL. */
    char formatado[32];
    int n = snprintf(formatado, sizeof formatado, "%#06x %04d %zu", 42u, -7, (size_t)12);
    if (n != 14 || strcmp(formatado, "0x002a -007 12")) FALHA(10, "snprintf/formatacao");
    char curto[5];
    if (snprintf(curto, sizeof curto, "abcdef") != 6 || strcmp(curto, "abcd"))
        FALHA(11, "snprintf truncado");
    if (snprintf(NULL, 0, "%s:%d", "x", 3) != 3) FALHA(12, "snprintf tamanho zero");
    if (snprintf(formatado, sizeof formatado, "%#.3o", 1u) != 3 || strcmp(formatado, "001"))
        FALHA(49, "printf octal precision");
    if (snprintf(formatado, sizeof formatado, "%lld", LLONG_MIN) != 20 ||
        strcmp(formatado, "-9223372036854775808")) FALHA(50, "printf int64");

    /* Erros de kernel viram -1/errno POSIX e O_EXCL/O_WRONLY sao respeitados. */
    errno = 0;
    if (open("/tmp/mlibc-ausente", O_RDONLY) != -1 || errno != ENOENT)
        FALHA(13, "traducao ENOENT");
    const char *path = "/tmp/mlibc-teste.txt";
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_TRUNC, 0600);
    if (fd < 0) FALHA(14, "open criacao");
    errno = 0;
    if (open(path, O_WRONLY | O_CREAT | O_EXCL, 0600) != -1 || errno != EEXIST)
        FALHA(15, "O_EXCL");
    char byte;
    errno = 0;
    if (read(fd, &byte, 1) != -1 || errno != EBADF) FALHA(16, "O_WRONLY/read");
    if (write(fd, "abcdef\n", 7) != 7) FALHA(17, "write");
    if (close(fd)) FALHA(18, "close");

    /* FILE: w+, fgets nao consome alem de n-1, EOF, seek e pushback. */
    FILE *f = fopen(path, "w+");
    if (!f) FALHA(19, "fopen w+");
    if (fputs("abcdef\n", f) == EOF || ftell(f) != 7) FALHA(20, "fputs/ftell");
    if (fseek(f, 0, SEEK_SET)) FALHA(21, "fseek");
    char linha[4];
    if (!fgets(linha, sizeof linha, f) || strcmp(linha, "abc")) FALHA(22, "fgets primeira parte");
    if (!fgets(linha, sizeof linha, f) || strcmp(linha, "def")) FALHA(23, "fgets segunda parte");
    if (!fgets(linha, sizeof linha, f) || strcmp(linha, "\n")) FALHA(24, "fgets newline");
    if (fgets(linha, sizeof linha, f) || !feof(f)) FALHA(25, "EOF");
    if (fseek(f, 0, SEEK_SET)) FALHA(26, "seek para fread");
    char itens[8];
    if (fread(itens, 2, 4, f) != 3 || memcmp(itens, "abcdef\n", 7) || !feof(f))
        FALHA(51, "fread itens parciais");
    if (fseek(f, 0, SEEK_SET)) FALHA(26, "seek para pushback");
    int c = fgetc(f);
    if (c != 'a' || ungetc(c, f) != 'a' || ftell(f) != 0 || fgetc(f) != 'a')
        FALHA(27, "ungetc");
    if (fclose(f)) FALHA(28, "fclose");

    /* O_RDWR, append, pipe, dup e fcntl basico. */
    fd = open(path, O_RDWR | O_APPEND);
    if (fd < 0 || (fcntl(fd, F_GETFL) & O_ACCMODE) != O_RDWR) FALHA(29, "F_GETFL");
    if (lseek(fd, 0, SEEK_END) != 7 || write(fd, "!", 1) != 1) FALHA(30, "lseek/append");
    int duplicate = dup(fd);
    if (duplicate < 0 || close(fd)) FALHA(31, "dup/close");
    if (write(duplicate, "?", 1) != 1 || close(duplicate)) FALHA(32, "dup compartilhado");

    int ends[2];
    if (pipe(ends)) FALHA(33, "pipe");
    if (write(ends[1], "ok", 2) != 2 || read(ends[0], formatado, 2) != 2 ||
        memcmp(formatado, "ok", 2)) FALHA(34, "pipe read/write");
    if (close(ends[0]) || close(ends[1])) FALHA(35, "fechar pipe");

    /* Relogio monotonic/realtime e sysdeps sem threads. */
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) || ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L)
        FALHA(36, "CLOCK_MONOTONIC");
    if (clock() < 0) FALHA(45, "clock CPU");
    if (clock_gettime(CLOCK_REALTIME, &ts) || ts.tv_sec <= 0) FALHA(37, "CLOCK_REALTIME");
    struct timespec curto_tempo = { 0, 1000000L };
    if (nanosleep(&curto_tempo, NULL)) FALHA(38, "nanosleep");
    time_t agora;
    if (time(&agora) == (time_t)-1 || agora <= 0) FALHA(39, "time");

    unsigned long ms = 0;
    if (mlibc_clock_get(&ms)) FALHA(40, "sysdep ClockGet");
    void *mem = mlibc_anon_allocate(16);
    if (!mem || mlibc_anon_free(mem, 16)) FALHA(41, "sysdep AnonAllocate/Free");
    if (mlibc_futex_wake(NULL, 0) || mlibc_tcb_set(NULL)) FALHA(42, "sysdeps single-thread");
    errno = 0;
    if (mlibc_futex_wait(NULL, 0, 0) != -1 || errno != ENOSYS) FALHA(43, "Futex indisponivel");

    if (remove(path)) FALHA(44, "remove");
    printf("[MLIBC] POSIX: heap, errno, strings, stdio, arquivos, pipe e tempo OK\n");
    printf("[MLIBC] sysdeps validos; uptime=%u ms\n", (unsigned)ms);
    return 0;
}
