/* tetris.c - Tetris no terminal do SaguiOS.
 *
 *   csagui build -o /tmp/tetris /jogos/tetris.c
 *   /tmp/tetris
 *
 * A/D movem a peca, W gira, S desce rapido, Q sai. O tabuleiro e desenhado
 * de novo a cada quadro no modo bruto. */
int pozo[180];
int base[28] = { 4,5,6,7,  1,2,5,6,  1,4,5,6,  1,2,4,5,  0,1,5,6,  0,4,5,6,  2,4,5,6 };
int cel[4];
int peca, rot, px, py, pontos, linhas, gravidade, proxima, perdeu;

static void recalcular(void)
{
    int k, ddy, ddx, r, t;
    for (k = 0; k < 4; k = k + 1) {
        ddy = base[peca * 4 + k] / 4;
        ddx = base[peca * 4 + k] % 4;
        r = rot;
        while (r > 0) {
            t = ddy;
            ddy = ddx;
            ddx = 3 - t;
            r = r - 1;
        }
        cel[k] = ddy * 4 + ddx;
    }
}

static int cabe(void)
{
    int k, ax, ay;
    for (k = 0; k < 4; k = k + 1) {
        ax = px + cel[k] % 4;
        ay = py + cel[k] / 4;
        if (ax < 0 || ax >= 10) return 0;
        if (ay >= 18) return 0;
        if (pozo[ay * 10 + ax] != 0) return 0;
    }
    return 1;
}

static void nova_peca(void)
{
    peca = proxima;
    rot = 0;
    px = 3;
    py = 0;
    proxima = aleatorio(7);
    recalcular();
    if (!cabe()) perdeu = 1;
}

static void travar(void)
{
    int k, ax, ay;
    for (k = 0; k < 4; k = k + 1) {
        ax = px + cel[k] % 4;
        ay = py + cel[k] / 4;
        if (ay >= 0 && ay < 18 && ax >= 0 && ax < 10)
            pozo[ay * 10 + ax] = 1;
    }
}

static void limpar_linhas_cheias(void)
{
    int y, x, yy, xx, cheia;
    for (y = 18 - 1; y >= 0; y = y - 1) {
        cheia = 1;
        for (x = 0; x < 10; x = x + 1) {
            if (pozo[y * 10 + x] == 0) cheia = 0;
        }
        if (cheia == 1) {
            for (yy = y; yy > 0; yy = yy - 1) {
                for (xx = 0; xx < 10; xx = xx + 1) {
                    pozo[yy * 10 + xx] = pozo[(yy - 1) * 10 + xx];
                }
            }
            for (xx = 0; xx < 10; xx = xx + 1) pozo[xx] = 0;
            linhas = linhas + 1;
            pontos = pontos + 250;
            y = y + 1;
        }
    }
}

static void gerar_peca_inicial(void)
{
    int i;
    for (i = 0; i < 180; i = i + 1) pozo[i] = 0;
    pontos = 0;
    linhas = 0;
    gravidade = 0;
    perdeu = 0;
    proxima = aleatorio(7);
    nova_peca();
}

static void desenhar(void)
{
    int row, col, k, marcado, eh_peca, ax, ay;
    limpar_tela();
    texto("  TETRIS   pontos "); numero(pontos); texto("   linhas "); numero(linhas); puts("");
    for (col = 0; col <= 10 + 1; col = col + 1) caractere(45);
    puts("");
    for (row = 0; row < 18; row = row + 1) {
        caractere(124);
        for (col = 0; col < 10; col = col + 1) {
            marcado = pozo[row * 10 + col];
            eh_peca = 0;
            for (k = 0; k < 4; k = k + 1) {
                ax = px + cel[k] % 4;
                ay = py + cel[k] / 4;
                if (col == ax && row == ay) eh_peca = 1;
            }
            if (eh_peca == 1) caractere(79);
            else if (marcado == 1) caractere(35);
            else caractere(32);
        }
        caractere(124);
        puts("");
    }
    for (col = 0; col <= 10 + 1; col = col + 1) caractere(45);
    puts("");
    puts("A/D movem, W gira, S desce, Q sai");
}

int main(void)
{
    int c;
    gerar_peca_inicial();
    entrada_bruta(1);
    for (;;) {
        dormir_ms(50);
        c = tecla_tentada();
        while (c >= 0) {
            if (c == 'q' || c == 'Q') { entrada_bruta(0); puts("Ate mais!"); return 0; }
            if (c == 'a' || c == 'A') { px = px - 1; if (!cabe()) px = px + 1; }
            if (c == 'd' || c == 'D') { px = px + 1; if (!cabe()) px = px - 1; }
            if (c == 's' || c == 'S') { py = py + 1; if (!cabe()) py = py - 1; }
            if (c == 'w' || c == 'W') {
                rot = (rot + 1) % 4;
                recalcular();
                if (!cabe()) { rot = (rot + 3) % 4; recalcular(); }
            }
            c = tecla_tentada();
        }
        gravidade = gravidade + 1;
        if (gravidade >= 3) {
            gravidade = 0;
            py = py + 1;
            if (!cabe()) {
                py = py - 1;
                travar();
                pontos = pontos + 10;
                limpar_linhas_cheias();
                nova_peca();
                if (perdeu == 1) {
                    entrada_bruta(0);
                    desenhar();
                    texto("Fim de jogo! Pontos: "); numero(pontos); puts("");
                    return 0;
                }
            }
        }
        desenhar();
    }
}


