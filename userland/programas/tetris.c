/* tetris - compila /jogos/tetris.c com o csagui dentro do SaguiOS e roda. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    char *compilar[] = { "/bin/csagui", "build", "-o", "/tmp/tetris", "/jogos/tetris.c", NULL };
    int pid = executar("/bin/csagui", compilar);
    if (pid < 0) return 1;
    int codigo = -1;
    if (esperar(pid, &codigo) != pid || codigo != 0) return 1;
    char *rodar[] = { "/tmp/tetris", NULL };
    pid = executar("/tmp/tetris", rodar);
    if (pid < 0) return 1;
    return esperar(pid, &codigo) == pid ? codigo : 1;
}
