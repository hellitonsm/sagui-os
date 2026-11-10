/* velha.c - Jogo da Velha (tic-tac-toe) grafico do SaguiOS.
 *
 * Compilado dentro do proprio sistema pelo tinycc:
 *   make /jogos/grafico/velha.mk
 *   ./velha
 *
 * Voce joga de X contra a CPU (O). Clique numa casa ou use as setas e
 * Enter; as teclas 1-9 tambem marcam (1 = canto superior esquerdo, na
 * ordem de leitura). R recomeca a partida e Q (ou Esc) fecha. */
#include <gui.h>

#define LARG 360
#define ALT  460
#define GX   36
#define GY   92
#define CEL  96

#define COR_X 0x2A6FD8
#define COR_O 0xD04020

static const int linhas[8][3] = {
    {0,1,2}, {3,4,5}, {6,7,8},
    {0,3,6}, {1,4,7}, {2,5,8},
    {0,4,8}, {2,4,6}
};

static int casa[9];
static int sel;
static int fim;             /* 0 jogando, 1 X venceu, 2 O venceu, 3 empate */
static int vitorias_x, vitorias_o, empates;
static uint32_t semente;
static jan_t *j;

static uint32_t aleatorio(void)
{
    semente = semente * 1103515245u + 12345u;
    return semente >> 8;
}

static int cheio(void)
{
    int i;
    for (i = 0; i < 9; i++)
        if (casa[i] == 0) return 0;
    return 1;
}

static int linha_vencedora(void)
{
    int i;
    for (i = 0; i < 8; i++) {
        int a = linhas[i][0], b = linhas[i][1], c = linhas[i][2];
        if (casa[a] != 0 && casa[a] == casa[b] && casa[a] == casa[c])
            return i;
    }
    return -1;
}

/* Casa em que "quem" fecha uma linha, ou -1. */
static int casa_de_vitoria(int quem)
{
    int i, k, p, conta, vazia;
    for (i = 0; i < 8; i++) {
        conta = 0;
        vazia = -1;
        for (k = 0; k < 3; k++) {
            p = linhas[i][k];
            if (casa[p] == quem) conta++;
            else if (casa[p] == 0) vazia = p;
        }
        if (conta == 2 && vazia >= 0) return vazia;
    }
    return -1;
}

static int casa_livre_aleatoria(void)
{
    int livres[9], n = 0, i;
    for (i = 0; i < 9; i++)
        if (casa[i] == 0) livres[n++] = i;
    if (n == 0) return -1;
    return livres[(int)(aleatorio() % (uint32_t)n)];
}

static void jogar_cpu(void)
{
    int p = casa_de_vitoria(2);          /* ganhar se puder */
    if (p < 0) p = casa_de_vitoria(1);   /* senao, bloquear o X */
    if (p < 0 && casa[4] == 0) p = 4;    /* centro */
    if (p < 0) {
        static const int cantos[4] = {0, 2, 6, 8};
        int i, ordem[4], n = 0;
        for (i = 0; i < 4; i++)
            if (casa[cantos[i]] == 0) ordem[n++] = cantos[i];
        if (n > 0) p = ordem[(int)(aleatorio() % (uint32_t)n)];
    }
    if (p < 0) p = casa_livre_aleatoria();
    if (p >= 0) casa[p] = 2;
}

static void atualizar_fim(void)
{
    int l = linha_vencedora();
    if (l >= 0) {
        fim = casa[linhas[l][0]];
        if (fim == 1) vitorias_x++;
        else vitorias_o++;
    } else if (cheio()) {
        fim = 3;
        empates++;
    }
}

static void jogar_humano(int p)
{
    if (fim != 0 || p < 0 || p > 8 || casa[p] != 0) return;
    casa[p] = 1;
    atualizar_fim();
    if (fim == 0) {
        jogar_cpu();
        atualizar_fim();
    }
    sel = p;
}

static void recomecar(void)
{
    int i;
    for (i = 0; i < 9; i++) casa[i] = 0;
    sel = 4;
    fim = 0;
}

static void circulo(int cx, int cy, int r, uint32_t cor)
{
    int x = 0, y = r, d = 3 - 2 * r;
    while (y >= x) {
        g_retangulo(j, cx + x - 1, cy + y - 1, 3, 3, cor);
        g_retangulo(j, cx + y - 1, cy + x - 1, 3, 3, cor);
        g_retangulo(j, cx - x - 1, cy + y - 1, 3, 3, cor);
        g_retangulo(j, cx - y - 1, cy + x - 1, 3, 3, cor);
        g_retangulo(j, cx + x - 1, cy - y - 1, 3, 3, cor);
        g_retangulo(j, cx + y - 1, cy - x - 1, 3, 3, cor);
        g_retangulo(j, cx - x - 1, cy - y - 1, 3, 3, cor);
        g_retangulo(j, cx - y - 1, cy - x - 1, 3, 3, cor);
        if (d < 0) d += 4 * x + 6;
        else { d += 4 * (x - y) + 10; y--; }
        x++;
    }
}

static void desenhar(void)
{
    int i, l, x, y, cx, cy, o;
    char t[64];
    const char *estado;

    g_retangulo(j, 0, 0, LARG, ALT, COR_FUNDO);
    g_texto(j, 20, 14, "JOGO DA VELHA", COR_TEXTO);

    for (i = 0; i < 9; i++) {
        x = GX + (i % 3) * CEL;
        y = GY + (i / 3) * CEL;
        g_retangulo(j, x, y, CEL, CEL, COR_BRANCO);
        if (i == sel && fim == 0)
            g_retangulo(j, x + 3, y + 3, CEL - 6, CEL - 6, 0xE2EEFA);
    }

    for (i = 0; i < 9; i++) {
        if (casa[i] == 0) continue;
        cx = GX + (i % 3) * CEL + CEL / 2;
        cy = GY + (i / 3) * CEL + CEL / 2;
        if (casa[i] == 1) {
            for (o = -2; o <= 2; o += 2) {
                g_linha(j, cx - 25 + o, cy - 25, cx + 25 + o, cy + 25, COR_X);
                g_linha(j, cx - 25 + o, cy + 25, cx + 25 + o, cy - 25, COR_X);
            }
        } else {
            circulo(cx, cy, 26, COR_O);
        }
    }

    for (i = 1; i < 3; i++) {
        g_retangulo(j, GX + i * CEL - 1, GY, 2, 3 * CEL, COR_TEXTO);
        g_retangulo(j, GX, GY + i * CEL - 1, 3 * CEL, 2, COR_TEXTO);
    }
    g_moldura(j, GX - 1, GY - 1, 3 * CEL + 2, 3 * CEL + 2, COR_TEXTO);

    l = linha_vencedora();
    if (l >= 0) {
        int a = linhas[l][0], b = linhas[l][2];
        int x0 = GX + (a % 3) * CEL + CEL / 2;
        int y0 = GY + (a / 3) * CEL + CEL / 2;
        int x1 = GX + (b % 3) * CEL + CEL / 2;
        int y1 = GY + (b / 3) * CEL + CEL / 2;
        for (o = -2; o <= 2; o += 2)
            g_linha(j, x0 + o, y0 + o, x1 + o, y1 + o, COR_DESTAQUE);
    }

    if (fim == 1) estado = "Voce venceu!";
    else if (fim == 2) estado = "A CPU venceu!";
    else if (fim == 3) estado = "Empate!";
    else estado = "Sua vez (X): clique ou use 1-9";
    g_texto(j, 20, GY + 3 * CEL + 14, estado, fim ? COR_DESTAQUE : COR_TEXTO);

    formatar_texto(t, sizeof t, "Voce %d   CPU %d   Empates %d",
                   vitorias_x, vitorias_o, empates);
    g_texto(j, 20, GY + 3 * CEL + 38, t, COR_TEXTO_FRACO);
    g_texto(j, 20, GY + 3 * CEL + 58, "Setas+Enter  R recomeca  Q sai",
            COR_TEXTO_FRACO);

    jan_mostrar(j);
}

int main(int argc, char **argv)
{
    evento_t e;
    int p;

    (void)argc;
    (void)argv;
    semente = tempo_ms() | 1;
    recomecar();
    j = jan_abrir("Jogo da Velha", LARG, ALT);
    if (!j) return 1;
    desenhar();

    for (;;) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) break;

        if (e.tipo == EV_CLIQUE && (e.botoes & 1)) {
            int c = (e.x - GX) / CEL;
            int l = (e.y - GY) / CEL;
            if (e.x >= GX && e.y >= GY && c >= 0 && c < 3 && l >= 0 && l < 3)
                jogar_humano(l * 3 + c);
        } else if (e.tipo == EV_TECLA) {
            if (e.tecla == 'q' || e.tecla == 'Q' || e.tecla == TECLA_ESC) break;
            else if (e.tecla == 'r' || e.tecla == 'R') recomecar();
            else if (e.tecla == TECLA_ESQUERDA && sel % 3 > 0) sel--;
            else if (e.tecla == TECLA_DIREITA && sel % 3 < 2) sel++;
            else if (e.tecla == TECLA_CIMA && sel / 3 > 0) sel -= 3;
            else if (e.tecla == TECLA_BAIXO && sel / 3 < 2) sel += 3;
            else if (e.tecla == '\n' || e.tecla == ' ') jogar_humano(sel);
            else if (e.tecla >= '1' && e.tecla <= '9') {
                p = (int)(e.tecla - '1');
                sel = p;
                jogar_humano(p);
            }
        }
        desenhar();
    }

    jan_fechar(j);
    return 0;
}
