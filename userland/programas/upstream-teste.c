/* upstream-teste - exercita a API POSIX do mlibc upstream (LIBC=upstream).
 *
 * Usa apenas cabecalhos do mlibc upstream; as chamadas de sistema do SaguiOS
 * sao feitas por ele (abrir/ler/fechar/escrever/sair). O ponto de entrada e
 * 'principal', como nos demais programas do SaguiOS; o crt1 do mlibc chama
 * main -> principal(argc, argv). */
#include <unistd.h>
#include <fcntl.h>

int principal(int argc, char **argv)
{
    const char msg[] = "ola, mlibc upstream no SaguiOS!\r\n";
    const char caminho[] = "/bin/upstream-teste";
    char buf[16];
    int fd;
    ssize_t n;

    (void)argc;
    (void)argv;

    write(STDOUT_FILENO, msg, sizeof(msg) - 1);

    /* abre e le o proprio binario (exercita abrir + ler + fechar no mlibc) */
    fd = open(caminho, O_RDONLY);
    if (fd >= 0) {
        n = read(fd, buf, sizeof(buf));
        (void)n;
        close(fd);
    }

    return 0;
}
