/* gui_lua.c - fumaca do kit grafico a partir do Lua, para os testes.
 *
 * Executa /bin/lua com scripts que exercitam a biblioteca gui sem esperar
 * interacao: cria uma janela, desenha, fecha e imprime o resultado. Nada
 * aqui usa ferramentas do hospedeiro. */
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
    char buf[1024];
    int codigo = -1;

    /* A biblioteca gui existe e e uma tabela. */
    char *tipo[] = {"/bin/lua", "-e", "print(type(gui))", NULL};
    if (capturar(tipo, buf, sizeof buf, &codigo) < 0) return 1;
    if (codigo != 0 || compara_texto(buf, "table\n")) {
        imprimir_em(ERROS, "[GUI] biblioteca: codigo %d, saida %s\n", codigo, buf);
        return 2;
    }
    registrar("[GUI] biblioteca gui embutida no Lua\n");

    /* As constantes de evento estao la. */
    char *constantes[] = {"/bin/lua", "-e", "print(gui.EV_CLIQUE, gui.EV_FECHAR, gui.LISTA_ATIVOU)", NULL};
    if (capturar(constantes, buf, sizeof buf, &codigo) < 0) return 3;
    if (codigo != 0 || !contem(buf, "1") || !contem(buf, "5") || !contem(buf, "2")) {
        imprimir_em(ERROS, "[GUI] constantes: codigo %d, saida %s\n", codigo, buf);
        return 4;
    }
    registrar("[GUI] constantes de evento e de lista\n");

    /* Abre, desenha e fecha uma janela sem interacao. */
    char *smoke[] = {"/bin/lua", "/testes/gui_lua.lua", NULL};
    if (capturar(smoke, buf, sizeof buf, &codigo) < 0) return 5;
    if (codigo != 0 || !contem(buf, "gui lua ok")) {
        imprimir_em(ERROS, "[GUI] smoke: codigo %d, saida %s\n", codigo, buf);
        return 6;
    }
    registrar("[GUI] janela, botao e texto criados e fechados\n");

    /* require("gui") devolve a mesma tabela. */
    char *req[] = {"/bin/lua", "-e", "local g = require('gui'); print(type(g.janela))", NULL};
    if (capturar(req, buf, sizeof buf, &codigo) < 0) return 7;
    if (codigo != 0 || !contem(buf, "function")) {
        imprimir_em(ERROS, "[GUI] require: codigo %d, saida %s\n", codigo, buf);
        return 8;
    }
    registrar("[GUI] require('gui') funciona\n");

    registrar("[GUI] kit grafico do Lua validado\n");
    imprimir("[GUI] kit grafico do Lua validado\n");
    return 0;
}
