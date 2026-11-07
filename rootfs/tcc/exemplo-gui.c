/* exemplo-gui.c - programa grafico em C "de verdade", compilado dentro do
 * SaguiOS pelo tinycc: uma janela com rotulo, caixa de texto e botao,
 * ligada contra libsaguigui.a.
 *
 *   tinycc -o /tmp/exemplo-gui /tcc/exemplo-gui.c -lsaguigui
 *   /tmp/exemplo-gui
 *
 * Sem argumento: abre a janela e entra no loop de eventos (interativo).
 * Com "--autoteste": exercita os componentes com eventos sinteticos e
 * sai, imprimindo os marcadores que o teste /testes/gui_tinycc confere.
 * Nada aqui espera interacao humana. */
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

static jan_t *abrir_janela(caixa_t *campo, botao_t *ok)
{
    jan_t *j = jan_abrir("Exemplo GUI", 240, 140);
    if (!j) return NULL;

    rotulo(j, 12, 10, "Caixa de texto:", COR_TEXTO);

    memset(campo, 0, sizeof *campo);
    campo->x = 12;
    campo->y = 30;
    campo->l = 216;
    campo->cursor = 0;
    campo->foco = true;

    memset(ok, 0, sizeof *ok);
    ok->x = 170;
    ok->y = 78;
    ok->l = 58;
    ok->a = 26;
    ok->texto = "OK";
    ok->apertado = false;

    rotulo(j, 12, 88, "Digite e clique OK", COR_TEXTO_FRACO);
    caixa_desenhar(j, campo);
    botao_desenhar(j, ok);
    jan_mostrar(j);
    return j;
}

int main(int argc, char **argv)
{
    caixa_t campo;
    botao_t ok;
    jan_t *j = abrir_janela(&campo, &ok);
    if (!j) {
        printf("[EXEMPLO-GUI] falha ao abrir a janela\n");
        return 1;
    }

    if (argc > 1 && strcmp(argv[1], "--autoteste") == 0) {
        /* Digita "oi" na caixa, como se fossem eventos do teclado. */
        caixa_tratar(j, &campo, &evento_simples(EV_TECLA, 0, 0, 'o'));
        caixa_tratar(j, &campo, &evento_simples(EV_TECLA, 0, 0, 'i'));
        printf("[EXEMPLO-GUI] caixa digitada: texto='%s' cursor=%d\n",
               campo.texto, campo.cursor);

        /* Aperta e solta o botao no mesmo ponto (clique sintetico). */
        evento_t clique = evento_simples(EV_CLIQUE, ok.x + 5, ok.y + 5, 0);
        evento_t soltar = evento_simples(EV_SOLTOU, ok.x + 5, ok.y + 5, 0);
        botao_tratar(j, &ok, &clique);
        int clicado = botao_tratar(j, &ok, &soltar);
        printf("[EXEMPLO-GUI] botao clicado: %s\n", clicado ? "sim" : "nao");

        jan_fechar(j);

        if (clicado && strcmp(campo.texto, "oi") == 0 && campo.cursor == 2) {
            printf("[EXEMPLO-GUI] programa grafico validado no convidado\n");
            return 0;
        }
        printf("[EXEMPLO-GUI] autoteste FALHOU\n");
        return 1;
    }

    /* Interativo: espera eventos ate o usuario fechar a janela. */
    printf("[EXEMPLO-GUI] janela interativa aberta (feche no X)\n");
    for (;;) {
        evento_t e;
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) break;
        caixa_tratar(j, &campo, &e);
        if (botao_tratar(j, &ok, &e))
            rotulo(j, 12, 116, campo.texto, COR_DESTAQUE);
        jan_mostrar(j);
    }
    jan_fechar(j);
    return 0;
}
