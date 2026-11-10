/* exemplo-gui.c - demonstracao VIVA e visivel da biblioteca grafica do
 * tinycc: uma janela de verdade, com relogio de uptime correndo, caixa de
 * texto que o proprio programa digita e botao que conta os proprios cliques.
 *
 *   tinycc /tcc/exemplo-gui.c -lsaguigui   # gera ./exemplo-gui (sem -o)
 *   ./exemplo-gui                # roda a demonstracao na tela (sozinha)
 *   ./exemplo-gui --interativo   # app para voce brincar (X fecha)
 *
 * A demonstracao abre a janela, espera 1 s, digita "SaguiOS" na caixa de
 * texto, clica no botao 3 vezes (contador sobe na tela), marca TUDO OK e
 * fecha sozinha ao final, imprimindo os marcadores que o teste
 * /testes/gui_tinycc confere. Nada aqui precisa de humano. */
#include <stdio.h>
#include <string.h>
#include <gui.h>

static evento_t evento_simples(unsigned tipo, int x, int y, unsigned tecla)
{
    evento_t e;
    memset(&e, 0, sizeof e);
    e.tipo = tipo;
    e.janela = 0;
    e.x = x;
    e.y = y;
    e.botoes = 1;          /* botao esquerdo */
    e.cliques = 1;
    e.tecla = tecla;
    e.modificadores = 0;
    return e;
}

static jan_t *abrir_janela(caixa_t *campo, botao_t *ok, char *rot_botao)
{
    jan_t *j = jan_abrir("Exemplo GUI - TinyCC", 340, 170);
    if (!j) return NULL;

    rotulo(j, 12, 8, "Compilado DENTRO do SaguiOS pelo tinycc", COR_TEXTO_FRACO);
    rotulo(j, 12, 30, "Caixa de texto:", COR_TEXTO);

    memset(campo, 0, sizeof *campo);
    campo->x = 12;
    campo->y = 48;
    campo->l = 316;
    campo->cursor = 0;
    campo->foco = true;

    snprintf(rot_botao, 32, "OK (0)");
    memset(ok, 0, sizeof *ok);
    ok->x = 244;
    ok->y = 82;
    ok->l = 84;
    ok->a = 26;
    ok->texto = rot_botao;
    ok->apertado = false;

    rotulo(j, 12, 88, "Cliquei 0 vezes", COR_TEXTO);
    rotulo(j, 12, 116, "Tempo ligado: 0.0 s", COR_TEXTO_FRACO);
    rotulo(j, 12, 140, "Feche a janela no X para sair.", COR_TEXTO_FRACO);
    caixa_desenhar(j, campo);
    botao_desenhar(j, ok);
    jan_mostrar(j);
    return j;
}

static void atualizar_cliques(jan_t *j, int n, char *rot_botao)
{
    char texto[32];
    snprintf(rot_botao, 32, "OK (%d)", n);
    snprintf(texto, sizeof texto, "Cliquei %d vezes", n);
    g_retangulo(j, 12, 88, 200, 16, COR_FUNDO);
    g_texto(j, 12, 88, texto, COR_TEXTO);
    jan_mostrar_area(j, 12, 88, 200, 16);
}

static void auto_clique(jan_t *j, botao_t *ok, caixa_t *campo, int *cliques, char *rot_botao)
{
    (void)campo;
    evento_t clique = evento_simples(EV_CLIQUE, ok->x + 10, ok->y + 10, 0);
    evento_t soltar = evento_simples(EV_SOLTOU, ok->x + 10, ok->y + 10, 0);
    atualizar_cliques(j, ++(*cliques), rot_botao);
    botao_tratar(j, ok, &clique);
    botao_tratar(j, ok, &soltar);
}

static void etapa_ok(jan_t *j)
{
    g_retangulo(j, 12, 138, 330, 20, COR_DESTAQUE);
    g_texto(j, 20, 140, "TUDO OK", COR_BRANCO);
    jan_mostrar_area(j, 12, 138, 330, 20);
}

int main(int argc, char **argv)
{
    int modo = 0;   /* 0 = demonstracao sozinha, 1 = --interativo */
    if (argc > 1 && strcmp(argv[1], "--interativo") == 0) modo = 1;

    char rot_botao[32];
    caixa_t campo;
    botao_t ok;
    jan_t *j = abrir_janela(&campo, &ok, rot_botao);
    if (!j) {
        printf("[EXEMPLO-GUI] falha ao abrir a janela (area de trabalho?)\n");
        return 1;
    }
    printf("[EXEMPLO-GUI] janela aberta pelo tinycc\n");

    unsigned inicio = tempo_ms();
    int cliques = 0, digitados = 0, pos = 0, autocliques = 0, validado = 0;
    unsigned ultimo_segundo = 0;
    const char *script = "SaguiOS";
    int script_len = (int)strlen(script);

    for (;;) {
        unsigned agora = tempo_ms() - inicio;

        /* trata o que o usuario fizer (ou nada, se ninguem mexer) */
        evento_t e;
        while (proximo_evento(j->id, &e, false) == 1) {
            if (e.tipo == EV_FECHAR) {
                jan_fechar(j);
                printf("[EXEMPLO-GUI] janela fechada\n");
                return 0;
            }
            caixa_tratar(j, &campo, &e);
            if (botao_tratar(j, &ok, &e)) {
                atualizar_cliques(j, ++cliques, rot_botao);
                printf("[EXEMPLO-GUI] clique do usuario registrado: %d\n", cliques);
            }
        }

        /* relogio de uptime (atualiza 1x por segundo) */
        if (agora / 1000 != ultimo_segundo) {
            char t[48];
            ultimo_segundo = agora / 1000;
            snprintf(t, sizeof t, "Tempo ligado: %lu.%lu s",
                     (unsigned long)(agora / 1000),
                     (unsigned long)((agora / 100) % 10));
            g_retangulo(j, 12, 116, 320, 16, COR_FUNDO);
            g_texto(j, 12, 116, t, COR_TEXTO_FRACO);
            jan_mostrar_area(j, 12, 116, 320, 16);
        }

        if (modo == 1) { dormir(50); continue; }

        /* ---- etapas da demonstracao (sem precisar de humano) ---- */
        if (pos < script_len && agora >= 1000 + (unsigned)pos * 150) {
            caixa_tratar(j, &campo, &evento_simples(EV_TECLA, 0, 0, (unsigned char)script[pos]));
            pos++;
            if (pos == script_len) {
                printf("[EXEMPLO-GUI] caixa recebeu '%s' auto-digitada\n", script);
                digitados = 1;
            }
        }
        if (autocliques < 3 && agora >= 3000 + (unsigned)autocliques * 400) {
            auto_clique(j, &ok, &campo, &cliques, rot_botao);
            autocliques++;
            if (autocliques == 3)
                printf("[EXEMPLO-GUI] botao clicado 3 vezes pelo proprio exemplo (contador=%d)\n", cliques);
        }
        if (!validado && digitados && autocliques == 3 && agora >= 4600) {
            etapa_ok(j);
            printf("[EXEMPLO-GUI] TUDO OK - componentes desenhados e eventos tratados\n");
            validado = 1;
        }
        if (agora >= 6200) break;

        dormir(50);
    }

    jan_fechar(j);
    printf("[EXEMPLO-GUI] demonstracao concluida pelo tinycc\n");
    return 0;
}
