/* pong - compila /jogos/pong.c com o csagui dentro do SaguiOS e roda. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    char *compilar[] = { "/bin/csagui", "build", "-o", "/tmp/pong", "/jogos/pong.c", NULL };
    int pid = executar("/bin/csagui", compilar);
    if (pid < 0) return 1;
    int codigo = -1;
    if (esperar(pid, &codigo) != pid || codigo != 0) return 1;
    char *rodar[] = { "/tmp/pong", NULL };
    pid = executar("/tmp/pong", rodar);
    if (pid < 0) return 1;
    return esperar(pid, &codigo) == pid ? codigo : 1;
}
