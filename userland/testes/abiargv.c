/* abiargv.c - diagnostico do ABI de inicializacao (H13): argc/argv chegam
 * ao anel 3 corretos? Este binario se executa de novo com argumentos, para
 * exercitar o SIS_EXECUTAR com vetor nao trivial. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    if (argc > 1) {
        /* filho: mostra exatamente o que recebeu */
        imprimir("[ABIARGV] argc=%d\n", argc);
        for (int i = 0; i < argc; i++)
            if (argv[i]) imprimir("[ABIARGV] argv[%d] = \"%s\" @ %p\n", i, argv[i], (void *)argv[i]);
        imprimir("[ABIARGV] terminador = %p\n", (void *)argv[argc]);
        return 0;
    }
    /* pai: lança o filho com argumentos */
    char *filho[] = { "/testes/abiargv", "um", "dois", "tr\xc3\xaa", "quatro com espa\xc3\xa7o", NULL };
    int r = executar("/testes/abiargv", filho);
    if (r < 0) {
        imprimir("[ABIARGV] executar: %d\n", r);
        return 1;
    }
    int codigo = -1;
    int esp = esperar(r, &codigo);
    imprimir("[ABIARGV] esperar=%d filho codigo=%d\n", esp, codigo);
    return 0;
}
