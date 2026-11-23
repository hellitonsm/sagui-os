/* sudoku - compila /jogos/sudoku.c com o csagui dentro do SaguiOS e roda.
 *
 * O jogo é um programa de terminal compilado pelo csagui (subconjunto de C)
 * no próprio convidado; este lançador só acompanha a compilação e repassa o
 * código de saída do jogo.
 */
#include "sagui.h"

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    char *compilar[] = { "/bin/csagui", "build", "-o", "/tmp/sudoku", "/jogos/sudoku.c", NULL };
    int pid = executar("/bin/csagui", compilar);
    if (pid < 0) return 1;
    int codigo = -1;
    if (esperar(pid, &codigo) != pid || codigo != 0) return 1;
    char *rodar[] = { "/tmp/sudoku", NULL };
    pid = executar("/tmp/sudoku", rodar);
    if (pid < 0) return 1;
    return esperar(pid, &codigo) == pid ? codigo : 1;
}
