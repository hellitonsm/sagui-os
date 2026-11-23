/* tabuada.c - Jogo da tabuada grafico do SaguiOS.
 *
 * O sistema mostra uma multiplicacao (ex.: 7 x 8 = ?) e voce digita a
 * resposta. Enter confirma, Backspace apaga, R gera outra conta, Q (ou Esc)
 * fecha. Pontos e tentativas ficam no topo. */
#include "sagui.h"
#include "gui.h"

#define LARG 420
#define ALT  260

static jan_t *j;
static int a, b;
static int resposta;      /* -1 = nada digitado ainda */
static int pontos, tentativas;
static int msg;           /* 0 nada, 1 acertou, 2 errou */
static int valor_certo;
static uint32_t semente;

static uint32_t aleat(void)
{
    semente = semente * 1103515245u + 12345u;
    return semente >> 16;
}

static void nova_conta(void)
{
    a = 2 + (int)(aleat() % 9);   /* 2..10 */
    b = 2 + (int)(aleat() % 9);
    resposta = -1;
    msg = 0;
}

static void desenhar(void)
{
    char t[64];

    g_retangulo(j, 0, 0, LARG, ALT, COR_FUNDO);
    g_texto(j, 20, 16, "TABUADA", COR_TEXTO);
    formatar_texto(t, sizeof t, "Pontos: %d / %d", pontos, tentativas);
    g_texto(j, 240, 16, t, COR_TEXTO_FRACO);

    /* pergunta */
    formatar_texto(t, sizeof t, "%d x %d = ?", a, b);
    g_texto(j, (LARG - g_largura_texto(t)) / 2, 80, t, COR_TEXTO);

    /* caixa da resposta */
    g_retangulo(j, (LARG - 140) / 2, 110, 140, 36, COR_BRANCO);
    g_moldura(j, (LARG - 140) / 2, 110, 140, 36, COR_BORDA);
    if (resposta >= 0)
        formatar_texto(t, sizeof t, "%d", resposta);
    else
        t[0] = 0;
    g_texto(j, (LARG - g_largura_texto(t)) / 2, 120, t, COR_SELECAO);

    /* mensagem */
    if (msg == 1) {
        g_texto(j, (LARG - g_largura_texto("Certo!")) / 2, 170, "Certo!", COR_DESTAQUE);
    } else if (msg == 2) {
        formatar_texto(t, sizeof t, "Errou! Era %d", valor_certo);
        g_texto(j, (LARG - g_largura_texto(t)) / 2, 170, t, 0xC02020);
    }

    g_texto(j, 20, ALT - 28, "Digite o numero e Enter  R nova conta  Q sai",
            COR_TEXTO_FRACO);
    jan_mostrar(j);
}

int principal(int argc, char **argv)
{
    evento_t e;

    (void)argc;
    (void)argv;
    semente = tempo_ms() | 1;
    pontos = 0;
    tentativas = 0;
    nova_conta();

    j = jan_abrir("Tabuada", LARG, ALT);
    if (!j) return 1;
    desenhar();

    for (;;) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) break;
        if (e.tipo != EV_TECLA) continue;

        if (e.tecla == 'q' || e.tecla == 'Q' || e.tecla == TECLA_ESC) break;
        if (e.tecla == 'r' || e.tecla == 'R') { nova_conta(); desenhar(); continue; }

        if (msg != 0) {
            /* apos responder, qualquer tecla (exceto q/r) gera nova conta */
            if (e.tecla == '\n' || e.tecla == ' ') { nova_conta(); desenhar(); }
            continue;
        }

        if (e.tecla >= '0' && e.tecla <= '9') {
            int dig = (int)(e.tecla - '0');
            if (resposta < 0) resposta = dig;
            else if (resposta < 1000) resposta = resposta * 10 + dig;
        } else if (e.tecla == '\b' || e.tecla == TECLA_DELETE) {
            if (resposta >= 0) resposta = resposta / 10;
            if (resposta == 0) resposta = -1;
        } else if (e.tecla == '\n') {
            if (resposta < 0) continue;
            tentativas++;
            valor_certo = a * b;
            if (resposta == valor_certo) { pontos++; msg = 1; }
            else msg = 2;
        }
        desenhar();
    }

    jan_fechar(j);
    return 0;
}
