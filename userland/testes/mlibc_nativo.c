/* mlibc_nativo - testa a camada mlibc (sysdeps) no proprio SaguiOS.
 * Usa os nomes POSIX (printf/malloc/open) como um programa C comum. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <mlibc/sysdeps.h>

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    /* 1. malloc/calloc/realloc/free (AnonAllocate/Free) */
    char *p = malloc(32);
    if (!p) return 1;
    strcpy(p, "sagui");
    if (strcmp(p, "sagui")) return 2;
    if (strlen(p) != 5) return 3;
    p = realloc(p, 64);
    if (!p) return 4;
    free(p);
    /* 2. printf/stdio (Write) */
    printf("[MLIBC] hello\n");
    /* 3. open/write/read/close (Open/Read/Seek/Close) */
    int fd = open("/tmp/mlibc-teste.txt", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return 5;
    if (write(fd, "ola", 3) != 3) return 6;
    close(fd);
    fd = open("/tmp/mlibc-teste.txt", O_RDONLY);
    if (fd < 0) return 7;
    char buf[8] = {0};
    if (read(fd, buf, 3) != 3) return 8;
    if (strcmp(buf, "ola")) return 9;
    close(fd);
    unlink("/tmp/mlibc-teste.txt");
    /* 4. sysdeps obrigatorios diretos */
    unsigned long ms = 0;
    if (mlibc_clock_get(&ms)) return 10;
    mlibc_libc_log("[MLIBC] sysdeps ok\n");
    if (mlibc_futex_wake(0, 0)) return 11;
    if (mlibc_tcb_set(0)) return 12;
    printf("[MLIBC] sysdeps validados\n");
    return 0;
}
