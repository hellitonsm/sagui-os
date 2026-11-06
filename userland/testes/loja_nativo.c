/* Testa a loja de apps no proprio SaguiOS. Uma feature por caso. */
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
    if (esperar(pid, &codigo) < 0) { fechar(f[0]); return -1; }
    int n = ler(f[0], buf, cap - 1); fechar(f[0]);
    if (n < 0) return -1;
    buf[n] = 0;
    return codigo;
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    char b[4096];
    /* 1. listar fonte local */
    char *listar[] = {"/bin/loja", "listar", NULL};
    if (rodar(listar, b, sizeof b) != 0) return 1;
    if (!acha_char(b, 'o') || !acha_char(b, 'a')) return 2;
    /* 2. instalar app Go local (/go/ola.go -> /store/ola) */
    char *inst_go[] = {"/bin/loja", "instalar", "ola", NULL};
    if (rodar(inst_go, b, sizeof b) != 0) return 3;
    if (!acha_char(b, 'i')) return 4;
    /* 3. o binario instalado executa */
    char *ola[] = {"/store/ola", NULL};
    if (rodar(ola, b, sizeof b) != 0) return 5;
    /* 4. configurar link da loja e reler */
    char *conf[] = {"/bin/loja", "configurar", "http://loja.exemplo/", NULL};
    if (rodar(conf, b, sizeof b) != 0) return 6;
    char *link[] = {"/bin/loja", "link", NULL};
    if (rodar(link, b, sizeof b) != 0) return 7;
    if (compara_texto(b, "loja: http://loja.exemplo/\n")) return 8;
    /* 5. instalar app Lua local (/lua/recursos.lua -> /store/recursos.lua) */
    char *inst_lua[] = {"/bin/loja", "instalar", "recursos", NULL};
    if (rodar(inst_lua, b, sizeof b) != 0) return 9;
    {
        info_arquivo_t inf;
        if (info_arquivo("/store/recursos.lua", &inf) < 0) return 10;
        apagar("/store/recursos.lua");
    }
    registrar("[LOJA] fonte local, instalacao Go e link validados\n");
    return 0;
}
