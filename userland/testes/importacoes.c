/* Testa importacoes embutidas, no proprio SaguiOS. */
#include "sagui.h"

static int rodar(char *const args[], char *buf, unsigned cap)
{
    int f[2], codigo = -1;
    if (criar_pipe(f) < 0) return -1;
    int a = duplicar(SAIDA, 14), b = duplicar(ERROS, 15);
    if (a < 0 || b < 0) {
        if (a >= 0) fechar(a);
        if (b >= 0) fechar(b);
        fechar(f[0]); fechar(f[1]); return -1;
    }
    duplicar(f[1], SAIDA); duplicar(f[1], ERROS);
    int pid = executar(args[0], args);
    duplicar(a, SAIDA); duplicar(b, ERROS); fechar(a); fechar(b); fechar(f[1]);
    if (pid < 0) { fechar(f[0]); return -1; }
    /* Os exemplos produzem menos que a capacidade do pipe. */
    if (esperar(pid, &codigo) < 0) { fechar(f[0]); return -1; }
    int n = ler(f[0], buf, cap - 1); fechar(f[0]);
    if (n < 0) return -1;
    buf[n] = 0;
    return codigo;
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    char b[512];
    char *build[] = {"/bin/gosagui", "build", "-o", "/tmp/teste-import-go", "/go/importacoes.go", NULL};
    if (rodar(build, b, sizeof b) != 0) return 1;
    char *go[] = {"/tmp/teste-import-go", NULL};
    if (rodar(go, b, sizeof b) != 0 || compara_texto(b, "Ola da biblioteca fmt!\naviso: biblioteca embutida\n")) return 2;
    apagar("/tmp/teste-import-go");
    char *lua[] = {"/bin/lua", "/lua/importacoes.lua", NULL};
    if (rodar(lua, b, sizeof b) != 0 || compara_texto(b, "SAGUI\t9\n")) return 3;
    char *identidade[] = {"/bin/lua", "-e", "local m = require('math'); math = nil; assert(require('math') == m); print(m.abs(-2))", NULL};
    if (rodar(identidade, b, sizeof b) != 0 || compara_texto(b, "2\n")) return 4;
    char *inexistente[] = {"/bin/lua", "-e", "require('inexistente')", NULL};
    if (rodar(inexistente, b, sizeof b) != 1) return 5;
    char *invalido[] = {"/bin/lua", "-e", "require({})", NULL};
    if (rodar(invalido, b, sizeof b) != 1) return 6;
    registrar("[IMPORT] bibliotecas embutidas validadas\n");
    return 0;
}
