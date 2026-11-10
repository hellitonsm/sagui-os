/* tetris.c - Tetris ("tretis") na interface grafica do SaguiOS.
 *
 * Compilado dentro do proprio sistema pelo tinycc:
 *   make /jogos/grafico/tetris.mk
 *   ./tetris
 *
 * Setas esquerda/direita movem (segure para repetir), seta para baixo
 * acelera a queda, W (ou seta para cima) gira, R recomeca e Q (ou Esc)
 * fecha. A peca cai sozinha e as linhas cheias somem. */
#include <gui.h>

#define LARG 380
#define ALT  500
#define GX   20
#define GY   56
#define CEL  22
#define COLS 10
#define ROWS 18

static const int base[28] = {
    4,5,6,7,
    1,2,5,6,
    1,4,5,6,
    1,2,4,5,
    0,1,5,6,
    0,4,5,6,
    2,4,5,6
};

static const uint32_t cores[8] = {
    0x000000,
    0x28C8E0,   /* I - ciano   */
    0xE8C020,   /* O - amarelo */
    0xA050D0,   /* T - roxo    */
    0x30B050,   /* S - verde   */
    0xE04040,   /* Z - vermelho*/
    0x3060D0,   /* J - azul    */
    0xE08020    /* L - laranja */
};

static int pozo[COLS * ROWS];
static int cel[4];
static int peca, rot, px, py, pontos, linhas, proxima, perdeu;
static uint32_t semente;
static jan_t *j;

static uint32_t aleatorio(void)
{
    semente = semente * 1103515245u + 12345u;
    return semente >> 8;
}

static int sortear(void) { return (int)(aleatorio() % 7); }

static void recalcular(void)
{
    int k, ddy, ddx, r, t;
    for (k = 0; k < 4; k++) {
        ddy = base[peca * 4 + k] / 4;
        ddx = base[peca * 4 + k] % 4;
        r = rot;
        while (r > 0) {
            t = ddy;
            ddy = ddx;
            ddx = 3 - t;
            r--;
        }
        cel[k] = ddy * 4 + ddx;
    }
}

static int cabe(void)
{
    int k, ax, ay;
    for (k = 0; k < 4; k++) {
        ax = px + cel[k] % 4;
        ay = py + cel[k] / 4;
        if (ax < 0 || ax >= COLS) return 0;
        if (ay >= ROWS) return 0;
        if (ay >= 0 && pozo[ay * COLS + ax] != 0) return 0;
    }
    return 1;
}

static void nova_peca(void)
{
    peca = proxima;
    proxima = sortear();
    rot = 0;
    px = 3;
    py = 0;
    recalcular();
    if (!cabe()) perdeu = 1;
}

static void travar(void)
{
    int k, ax, ay;
    for (k = 0; k < 4; k++) {
        ax = px + cel[k] % 4;
        ay = py + cel[k] / 4;
        if (ay >= 0 && ay < ROWS && ax >= 0 && ax < COLS)
            pozo[ay * COLS + ax] = peca + 1;
    }
}

static void limpar_linhas(void)
{
    int y, x, yy, xx, cheia;
    for (y = ROWS - 1; y >= 0; y--) {
        cheia = 1;
        for (x = 0; x < COLS; x++)
            if (pozo[y * COLS + x] == 0) cheia = 0;
        if (cheia) {
            for (yy = y; yy > 0; yy--)
                for (xx = 0; xx < COLS; xx++)
                    pozo[yy * COLS + xx] = pozo[(yy - 1) * COLS + xx];
            for (xx = 0; xx < COLS; xx++) pozo[xx] = 0;
            linhas++;
            pontos += 250;
            y++;
        }
    }
}

static void recomecar(void)
{
    int i;
    for (i = 0; i < COLS * ROWS; i++) pozo[i] = 0;
    pontos = 0;
    linhas = 0;
    perdeu = 0;
    proxima = sortear();
    nova_peca();
}

static void mover(int dx)
{
    if (perdeu) return;
    px += dx;
    if (!cabe()) px -= dx;
}

static void girar(void)
{
    int antes;
    if (perdeu) return;
    antes = rot;
    rot = (rot + 1) % 4;
    recalcular();
    if (!cabe()) {
        rot = antes;
        recalcular();
    }
}

static void descer(void)
{
    if (perdeu) return;
    py++;
    if (!cabe()) {
        py--;
        travar();
        pontos += 10;
        limpar_linhas();
        nova_peca();
    }
}

static void bloco(int x, int y, int tam, uint32_t cor)
{
    g_retangulo(j, x + 1, y + 1, tam - 2, tam - 2, cor);
}

static void desenhar_peca(int x, int y, int tam, int qual, int giro)
{
    int k, ddy, ddx, r, t, ax, ay;
    for (k = 0; k < 4; k++) {
        ddy = base[qual * 4 + k] / 4;
        ddx = base[qual * 4 + k] % 4;
        r = giro;
        while (r > 0) {
            t = ddy;
            ddy = ddx;
            ddx = 3 - t;
            r--;
        }
        ax = x + ddx * tam;
        ay = y + ddy * tam;
        bloco(ax, ay, tam, cores[qual + 1]);
    }
}

static void desenhar(void)
{
    int row, col, k, ax, ay, cor;
    char t[48];

    g_retangulo(j, 0, 0, LARG, ALT, COR_FUNDO);
    g_texto(j, 20, 14, "TRETIS", COR_TEXTO);
    formatar_texto(t, sizeof t, "Pontos: %d", pontos);
    g_texto(j, 150, 14, t, COR_TEXTO);
    formatar_texto(t, sizeof t, "Linhas: %d", linhas);
    g_texto(j, 270, 14, t, COR_TEXTO);

    g_retangulo(j, GX, GY, COLS * CEL, ROWS * CEL, 0x18222C);

    for (row = 0; row < ROWS; row++)
        for (col = 0; col < COLS; col++) {
            cor = pozo[row * COLS + col];
            if (cor != 0)
                bloco(GX + col * CEL, GY + row * CEL, CEL, cores[cor]);
        }

    if (!perdeu)
        for (k = 0; k < 4; k++) {
            ax = px + cel[k] % 4;
            ay = py + cel[k] / 4;
            if (ay >= 0 && ay < ROWS && ax >= 0 && ax < COLS)
                bloco(GX + ax * CEL, GY + ay * CEL, CEL, cores[peca + 1]);
        }

    g_moldura(j, GX - 1, GY - 1, COLS * CEL + 2, ROWS * CEL + 2, COR_TEXTO);

    g_texto(j, 262, 60, "PROXIMA", COR_TEXTO_FRACO);
    g_retangulo(j, 262, 82, 4 * 15, 4 * 15, 0x18222C);
    desenhar_peca(262, 82, 15, proxima, 0);

    g_texto(j, 262, 170, "PONTOS", COR_TEXTO_FRACO);
    formatar_texto(t, sizeof t, "%d", pontos);
    g_texto(j, 262, 190, t, COR_DESTAQUE);
    g_texto(j, 262, 220, "LINHAS", COR_TEXTO_FRACO);
    formatar_texto(t, sizeof t, "%d", linhas);
    g_texto(j, 262, 240, t, COR_DESTAQUE);

    g_texto(j, 262, 290, "SETAS", COR_TEXTO_FRACO);
    g_texto(j, 262, 308, "movem", COR_TEXTO);
    g_texto(j, 262, 332, "W gira", COR_TEXTO);
    g_texto(j, 262, 356, "R recomeca", COR_TEXTO);
    g_texto(j, 262, 380, "Q sai", COR_TEXTO);

    if (perdeu) {
        g_retangulo(j, GX + 8, GY + 150, COLS * CEL - 16, 90, 0x18222C);
        g_moldura(j, GX + 8, GY + 150, COLS * CEL - 16, 90, 0xE04040);
        g_texto(j, GX + 26, GY + 168, "FIM DE JOGO", 0xE04040);
        g_texto(j, GX + 22, GY + 200, "R recomeca", COR_BRANCO);
        g_texto(j, GX + 22, GY + 220, "Q sai", COR_BRANCO);
    }

    jan_mostrar(j);
}

int main(int argc, char **argv)
{
    evento_t e;
    unsigned char teclas[32];
    int esq, dir, baixo, antes_esq = 0, antes_dir = 0, antes_baixo = 0;
    int rep_esq = 0, rep_dir = 0, rep_baixo = 0;
    int gravidade = 0, mudou = 1, intervalo;

    (void)argc;
    (void)argv;
    semente = tempo_ms() | 1;
    recomecar();

    j = jan_abrir("Tretis", LARG, ALT);
    if (!j) return 1;

    for (;;) {
        while (proximo_evento(j->id, &e, false) == 1) {
            if (e.tipo == EV_FECHAR) { jan_fechar(j); return 0; }
            if (e.tipo != EV_TECLA) continue;
            if (e.tecla == 'q' || e.tecla == 'Q' || e.tecla == TECLA_ESC) {
                jan_fechar(j);
                return 0;
            }
            if (e.tecla == 'r' || e.tecla == 'R') {
                recomecar();
                mudou = 1;
            } else if (perdeu) {
                continue;
            } else if (e.tecla == 'w' || e.tecla == 'W' || e.tecla == TECLA_CIMA) {
                girar();
                mudou = 1;
            } else if (e.tecla == 'a' || e.tecla == 'A') {
                mover(-1);
                mudou = 1;
            } else if (e.tecla == 'd' || e.tecla == 'D') {
                mover(1);
                mudou = 1;
            } else if (e.tecla == 's' || e.tecla == 'S') {
                descer();
                mudou = 1;
            }
        }

        teclas_seguradas(teclas);
        esq = SEGURADA(teclas, CV_ESQUERDA);
        dir = SEGURADA(teclas, CV_DIREITA);
        baixo = SEGURADA(teclas, CV_BAIXO);

        if (esq) {
            if (!antes_esq || rep_esq <= 0) { mover(-1); rep_esq = 4; mudou = 1; }
            else rep_esq--;
        } else rep_esq = 0;
        if (dir) {
            if (!antes_dir || rep_dir <= 0) { mover(1); rep_dir = 4; mudou = 1; }
            else rep_dir--;
        } else rep_dir = 0;
        if (baixo) {
            if (!antes_baixo || rep_baixo <= 0) { descer(); rep_baixo = 2; mudou = 1; }
            else rep_baixo--;
        } else rep_baixo = 0;
        antes_esq = esq;
        antes_dir = dir;
        antes_baixo = baixo;

        if (!perdeu) {
            intervalo = 30 - linhas;
            if (intervalo < 6) intervalo = 6;
            gravidade++;
            if (gravidade >= intervalo) {
                gravidade = 0;
                descer();
                mudou = 1;
            }
        }

        if (mudou) {
            desenhar();
            mudou = 0;
        }
        dormir(30);
    }
}
