/* gui_tinycc.c - teste da biblioteca grafica do tinycc no convidado:
 * compila o exemplo /tcc/exemplo-gui.c ligando contra libsaguigui.a
 * (/usr/lib/libsaguigui.a no sysroot) e executa a DEMONSTRACAO VISIVEL
 * (janela real, relogio, auto-digitacao, auto-cliques), conferindo os
 * marcadores que o proprio exemplo imprime. */
#include "sagui.h"

static int rodar(char *const args[])
{
    int codigo = -1;
    int pid = executar(args[0], args);
    if (pid < 0 || esperar(pid, &codigo) != pid) return -1;
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

    /* Compila o exemplo grafico ligando a biblioteca do tinycc. */
    char *tcc[] = { "/bin/tinycc", "-static", "-o", "/tmp/exemplo-gui",
                    "/tcc/exemplo-gui.c", "-lsaguigui", NULL };
    codigo = rodar(tcc);
    if (codigo != 0) {
        imprimir_em(ERROS, "[GUI-TINYCC] tinycc -lsaguigui: codigo %d\n", codigo);
        return 1;
    }
    registrar("[GUI-TINYCC] exemplo compilado e linkado com -lsaguigui\n");

    /* Roda a DEMONSTRACAO na tela: a janela abre, o relogio corre, a
     * caixa se digita sozinha e o botao conta os proprios cliques.
     * O programa fecha sozinho ao final (codigo 0). */
    char *exemplo[] = { "/tmp/exemplo-gui", NULL };
    if (capturar(exemplo, buf, sizeof buf, &codigo) < 0 || codigo != 0 ||
        !contem(buf, "janela aberta pelo tinycc") ||
        !contem(buf, "caixa recebeu 'SaguiOS' auto-digitada") ||
        !contem(buf, "botao clicado 3 vezes") ||
        !contem(buf, "TUDO OK") ||
        !contem(buf, "demonstracao concluida")) {
        imprimir_em(ERROS, "[GUI-TINYCC] exemplo: codigo %d, saida %s\n", codigo, buf);
        return 2;
    }
    apagar("/tmp/exemplo-gui");
    registrar("[GUI-TINYCC] janela demonstrada ao vivo no convidado\n");
    imprimir("%s", buf);   /* mostra a demonstracao na janela do Terminal */
    registrar("[GUI-TINYCC] biblioteca grafica do tinycc validada\n");
    imprimir("[GUI-TINYCC] biblioteca grafica do tinycc validada\n");
    return 0;
}
