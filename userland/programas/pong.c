/* pong.c - Pong grafico do SaguiOS.
 *
 * Voce controla a raquete da esquerda (W/S ou setas cima/baixo).
 * O CPU controla a raquete da direita. A bola rebate nas raquetes e nas
 * bordas de cima e de baixo. Primeiro a 5 pontos vence.
 *
 * Q (ou Esc) fecha, R recomeca. */
#include "sagui.h"
#include "gui.h"

/* Códigos de tecla no mapa de teclas_seguradas (scancodes do conjunto 1). */
#ifndef CV_W
#define CV_W 0x11
#endif
#ifndef CV_S
#define CV_S 0x1F
#endif
#ifndef CV_R
#define CV_R 0x13
#endif

#define LARG 480
#define ALT  320
#define TOPO 40       /* faixa do placar */
#define RAQ_L 8
#define RAQ_A 56
#define BOLA 8
#define RAQ_X_JOG 20
#define RAQ_X_CPU (LARG - 20 - RAQ_L)

static jan_t *j;
static int py_jog, py_cpu;
static int bx, by, bvx, bvy;
static int pontos_jog, pontos_cpu;
static uint32_t semente;

static uint32_t aleat(void)
{
    semente = semente * 1103515245u + 12345u;
    return semente >> 16;
}

static void reiniciar_bola(int dir)
{
    bx = LARG / 2 - BOLA / 2;
    by = TOPO + (int)(aleat() % (ALT - TOPO - BOLA - 20)) + 10;
    bvx = dir;
    bvy = (aleat() & 1) ? 2 : -2;
}

static void recomecar(void)
{
    py_jog = TOPO + (ALT - TOPO - RAQ_A) / 2;
    py_cpu = py_jog;
    pontos_jog = 0;
    pontos_cpu = 0;
    reiniciar_bola((aleat() & 1) ? 3 : -3);
}

static void desenhar(void)
{
    char t[48];

    /* fundo */
    g_retangulo(j, 0, 0, LARG, ALT, 0x101820);
    /* faixa do placar */
    g_retangulo(j, 0, 0, LARG, TOPO, 0x182838);
    formatar_texto(t, sizeof t, "Jogador %d  x  %d CPU", pontos_jog, pontos_cpu);
    g_texto(j, (LARG - g_largura_texto(t)) / 2, 12, t, COR_BRANCO);

    /* linha central tracejada */
    for (int y = TOPO + 8; y < ALT - 8; y += 16)
        g_retangulo(j, LARG / 2 - 1, y, 2, 8, 0x305060);

    /* raquetes e bola */
    g_retangulo(j, RAQ_X_JOG, py_jog, RAQ_L, RAQ_A, 0x50D060);
    g_retangulo(j, RAQ_X_CPU, py_cpu, RAQ_L, RAQ_A, 0xE05050);
    g_retangulo(j, bx, by, BOLA, BOLA, COR_BRANCO);

    /* instrucoes / fim */
    if (pontos_jog >= 5 || pontos_cpu >= 5) {
        const char *msg = pontos_jog >= 5 ? "Voce venceu!  R recomeca  Q sai"
                                           : "O CPU venceu!  R recomeca  Q sai";
        g_texto(j, (LARG - g_largura_texto(msg)) / 2, ALT - 24, msg, 0xFFD040);
    } else {
        g_texto(j, 10, ALT - 20, "W/S ou setas movem  Q sai", 0x8090A0);
    }
    jan_mostrar(j);
}

int principal(int argc, char **argv)
{
    evento_t e;
    unsigned char teclas[32];
    int antes_r = 0;

    (void)argc;
    (void)argv;
    semente = tempo_ms() | 1;
    recomecar();

    j = jan_abrir("Pong", LARG, ALT);
    if (!j) return 1;
    desenhar();

    for (;;) {
        while (proximo_evento(j->id, &e, false) == 1) {
            if (e.tipo == EV_FECHAR) { jan_fechar(j); return 0; }
            if (e.tipo == EV_TECLA) {
                if (e.tecla == 'q' || e.tecla == 'Q' || e.tecla == TECLA_ESC) {
                    jan_fechar(j);
                    return 0;
                }
            }
        }

        teclas_seguradas(teclas);
        /* R recomeca (borda de subida para nao repetir) */
        {
            int r = SEGURADA(teclas, CV_R);
            if (r && !antes_r) recomecar();
            antes_r = r;
        }

        if (pontos_jog < 5 && pontos_cpu < 5) {
            /* movimento do jogador */
            if (SEGURADA(teclas, CV_CIMA) || SEGURADA(teclas, CV_W)) py_jog -= 4;
            if (SEGURADA(teclas, CV_BAIXO) || SEGURADA(teclas, CV_S)) py_jog += 4;
            if (py_jog < TOPO) py_jog = TOPO;
            if (py_jog > ALT - RAQ_A) py_jog = ALT - RAQ_A;

            /* CPU simples: segue a bola com um pouco de atraso */
            {
                int centro_cpu = py_cpu + RAQ_A / 2;
                int centro_bola = by + BOLA / 2;
                if (centro_cpu < centro_bola - 8) py_cpu += 3;
                else if (centro_cpu > centro_bola + 8) py_cpu -= 3;
                if (py_cpu < TOPO) py_cpu = TOPO;
                if (py_cpu > ALT - RAQ_A) py_cpu = ALT - RAQ_A;
            }

            /* bola */
            bx += bvx;
            by += bvy;
            if (by <= TOPO) { by = TOPO; bvy = -bvy; }
            if (by + BOLA >= ALT) { by = ALT - BOLA; bvy = -bvy; }

            /* rebate na raquete do jogador */
            if (bvx < 0 && bx <= RAQ_X_JOG + RAQ_L && bx + BOLA >= RAQ_X_JOG &&
                by + BOLA >= py_jog && by <= py_jog + RAQ_A) {
                bx = RAQ_X_JOG + RAQ_L;
                bvx = -bvx;
                if (bvx < 6) bvx++;
                /* angulo conforme ponto de impacto */
                bvy = ((by + BOLA / 2) - (py_jog + RAQ_A / 2)) / 6;
                if (bvy == 0) bvy = (aleat() & 1) ? 1 : -1;
            }
            /* rebate na raquete do CPU */
            if (bvx > 0 && bx + BOLA >= RAQ_X_CPU && bx <= RAQ_X_CPU + RAQ_L &&
                by + BOLA >= py_cpu && by <= py_cpu + RAQ_A) {
                bx = RAQ_X_CPU - BOLA;
                bvx = -bvx;
                if (bvx > -6) bvx--;
                bvy = ((by + BOLA / 2) - (py_cpu + RAQ_A / 2)) / 6;
                if (bvy == 0) bvy = (aleat() & 1) ? 1 : -1;
            }

            /* ponto */
            if (bx + BOLA < 0) { pontos_cpu++; reiniciar_bola(3); }
            else if (bx > LARG) { pontos_jog++; reiniciar_bola(-3); }
        }

        desenhar();
        dormir(16);
    }
}
