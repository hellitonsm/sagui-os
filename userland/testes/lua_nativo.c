/* lua_nativo.c - roda os scripts Lua embutidos e confere a saida.
 *
 * O interpretador e um programa comum do anel 3 (/bin/lua). Este teste
 * executa scripts de /lua, captura a saida (inclusive os erros) por um pipe
 * e valida o que voltou. Nada aqui usa ferramentas do hospedeiro. */
#include "sagui.h"

static int rodar(char *const args[])
{
    int codigo = -1;
    int pid = executar(args[0], args);
    if (pid < 0 || esperar(pid, &codigo) < 0) return -1;
    return codigo;
}

/* Roda o programa com a saida e os erros num pipe; devolve o codigo e o
 * texto produzido. */
static int capturar(char *const args[], char *buf, unsigned max, int *codigo)
{
    int fds[2];
    if (criar_pipe(fds) < 0) return -1;
    int guarda1 = duplicar(SAIDA, 14);
    int guarda2 = duplicar(ERROS, 15);
    if (guarda1 < 0 || guarda2 < 0) {
        fechar(fds[0]); fechar(fds[1]);
        if (guarda1 >= 0) fechar(guarda1);
        if (guarda2 >= 0) fechar(guarda2);
        return -1;
    }
    duplicar(fds[1], SAIDA);
    duplicar(fds[1], ERROS);
    *codigo = rodar(args);
    duplicar(guarda1, SAIDA);
    duplicar(guarda2, ERROS);
    fechar(guarda1);
    fechar(guarda2);
    fechar(fds[1]);
    int n = ler(fds[0], buf, max - 1);
    fechar(fds[0]);
    if (n < 0) return -1;
    buf[n] = 0;
    return n;
}

static bool contem(const char *texto, const char *pedaco)
{
    size_t n = tam_texto(pedaco);
    for (const char *p = texto; *p; p++)
        if (!compara_texto_n(p, pedaco, n)) return true;
    return false;
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    char buf[4096];
    int codigo = -1;

    char *ola[] = {"/bin/lua", "/lua/ola.lua", NULL};
    if (capturar(ola, buf, sizeof buf, &codigo) < 0) return 1;
    if (codigo != 0 || compara_texto(buf, "Ola, Lua no SaguiOS!\nsoma\t55\n")) {
        imprimir_em(ERROS, "[LUA] ola: codigo %d, saida %s\n", codigo, buf);
        return 2;
    }
    registrar("[LUA] script basico e laco numerico\n");

    char *rec[] = {"/bin/lua", "/lua/recursos.lua", NULL};
    if (capturar(rec, buf, sizeof buf, &codigo) < 0) return 3;
    if (codigo != 0) return 4;
    const char *esperado[] = {
        "fatorial(6) =\t720",
        "soma(1,2,3,4) =\t10",
        "closure:\t1\t2\t3",
        "t[2] =\t20\tnome =\tsagui\t#t =\t4",
        "insert:\t10,15,20,30,40",
        "sort:\t15,20,30,40",
        "string:\tSAGUI\tSagui",
        "format:\tx=007",
        "find:\t3\t4",
        "math:\t9\t2\t5",
        "pares:\t10",
        "repeat:\t3",
        "while/break:\t4",
        "pairs:\ta1b2",
        "multiplos:\t1\t2",
        "tostring:\t42\ttable\tstring\tnil"
    };
    for (unsigned i = 0; i < sizeof esperado / sizeof esperado[0]; i++) {
        if (!contem(buf, esperado[i])) {
            imprimir_em(ERROS, "[LUA] faltou %s\n", esperado[i]);
            return 5;
        }
    }
    registrar("[LUA] funcoes, tabelas e bibliotecas\n");

    char *arq[] = {"/bin/lua", "/lua/arquivos.lua", NULL};
    if (capturar(arq, buf, sizeof buf, &codigo) < 0) return 6;
    if (codigo != 0 || !contem(buf, "arquivo:\tlinha 1\tlinha 2") ||
        !contem(buf, "linha:\tlinha 1") || !contem(buf, "linha:\tlinha 2") ||
        !contem(buf, "dofile funcionou")) {
        imprimir_em(ERROS, "[LUA] arquivos: codigo %d, saida %s\n", codigo, buf);
        return 7;
    }
    registrar("[LUA] arquivos e dofile\n");

    /* Erro de execucao: mensagem no erro e codigo 1. */
    char *erro[] = {"/bin/lua", "/lua/erro.lua", NULL};
    if (capturar(erro, buf, sizeof buf, &codigo) < 0) return 8;
    if (codigo != 1 || !contem(buf, "antes do erro") || !contem(buf, "falhou de proposito")) {
        imprimir_em(ERROS, "[LUA] erro: codigo %d, saida %s\n", codigo, buf);
        return 9;
    }
    registrar("[LUA] erro de execucao\n");

    /* Erro de sintaxe: tambem codigo 1, com a linha. */
    char *sint[] = {"/bin/lua", "/lua/sintaxe.lua", NULL};
    if (capturar(sint, buf, sizeof buf, &codigo) < 0) return 10;
    if (codigo != 1 || !contem(buf, "lua: linha")) {
        imprimir_em(ERROS, "[LUA] sintaxe: codigo %d, saida %s\n", codigo, buf);
        return 11;
    }
    registrar("[LUA] erro de sintaxe\n");

    /* -e e -v. */
    char *expr[] = {"/bin/lua", "-e", "print(2 + 3)", NULL};
    if (capturar(expr, buf, sizeof buf, &codigo) < 0) return 12;
    if (codigo != 0 || compara_texto(buf, "5\n")) {
        imprimir_em(ERROS, "[LUA] -e: codigo %d, saida %s\n", codigo, buf);
        return 13;
    }
    char *versao[] = {"/bin/lua", "-v", NULL};
    if (capturar(versao, buf, sizeof buf, &codigo) < 0) return 14;
    if (codigo != 0 || !contem(buf, "lua 0.1")) return 15;
    registrar("[LUA] -e e -v\n");

    registrar("[LUA] interpretador nativo validado\n");
    imprimir("[LUA] interpretador nativo validado\n");
    return 0;
}
