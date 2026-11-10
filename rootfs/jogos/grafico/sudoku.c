/* sudoku.c - Sudoku 9x9 na interface grafica do SaguiOS.
 *
 * Compilado dentro do proprio sistema pelo tinycc:
 *   make /jogos/grafico/sudoku.mk
 *   ./sudoku
 *
 * Setas movem a selecao, 1-9 colocam o numero, 0 (ou Backspace) apaga,
 * R recomeca e Q (ou Esc) fecha. Tambem da para clicar numa casa.
 * As casas com numero repetido na linha, coluna ou bloco ficam vermelhas. */
#include <gui.h>

#define LARG 436
#define ALT  512
#define GX   20
#define GY   76
#define CEL  44

static int tab[81] = {
    5,3,0,0,7,0,0,0,0,
    6,0,0,1,9,5,0,0,0,
    0,9,8,0,0,0,0,6,0,
    8,0,0,0,6,0,0,0,3,
    4,0,0,8,0,3,0,0,1,
    7,0,0,0,2,0,0,0,6,
    0,6,0,0,0,0,2,8,0,
    0,0,0,4,1,9,0,0,5,
    0,0,0,0,8,0,0,7,9
};
static int inicial[81];
static int fixo[81];
static int sel;
static int venceu;
static jan_t *j;

static void preparar(void)
{
    int i;
    for (i = 0; i < 81; i++) {
        inicial[i] = tab[i];
        fixo[i] = tab[i] != 0;
    }
    sel = 0;
    venceu = 0;
}

/* O valor v pode entrar na casa pos? Ignora a propria casa. */
static int pode_por(int pos, int v)
{
    int l = pos / 9, c = pos % 9, i, y, x, p;
    for (i = 0; i < 9; i++) {
        if (i != c && tab[l * 9 + i] == v) return 0;
        if (i != l && tab[i * 9 + c] == v) return 0;
    }
    for (y = 0; y < 3; y++)
        for (x = 0; x < 3; x++) {
            p = ((l / 3) * 3 + y) * 9 + (c / 3) * 3 + x;
            if (p != pos && tab[p] == v) return 0;
        }
    return 1;
}

static int conflito(int pos)
{
    if (tab[pos] == 0) return 0;
    return !pode_por(pos, tab[pos]);
}

static int mesma_caixa(int a, int b)
{
    return (a / 9) / 3 == (b / 9) / 3 && (a % 9) / 3 == (b % 9) / 3;
}

static int terminou(void)
{
    int i;
    for (i = 0; i < 81; i++)
        if (tab[i] == 0 || conflito(i)) return 0;
    return 1;
}

static void desenhar(void)
{
    int p, l, c, x, y, i;
    uint32_t cor, fundo;
    char t[2];

    g_retangulo(j, 0, 0, LARG, ALT, COR_FUNDO);
    g_texto(j, 20, 14, "SUDOKU", COR_TEXTO);
    g_texto(j, 20, 40, "Setas movem  1-9 poe  0 apaga  R recomeca  Q sai",
            COR_TEXTO_FRACO);

    for (p = 0; p < 81; p++) {
        l = p / 9;
        c = p % 9;
        x = GX + c * CEL;
        y = GY + l * CEL;
        fundo = COR_BRANCO;
        if (l == sel / 9 || c == sel % 9 || mesma_caixa(p, sel))
            fundo = 0xE8EFF8;
        if (p == sel)
            fundo = 0xC9DDF5;
        g_retangulo(j, x, y, CEL, CEL, fundo);
    }

    for (p = 0; p < 81; p++) {
        if (tab[p] == 0) continue;
        l = p / 9;
        c = p % 9;
        x = GX + c * CEL + (CEL - LETRA_L) / 2;
        y = GY + l * CEL + (CEL - LETRA_A) / 2;
        t[0] = (char)('0' + tab[p]);
        t[1] = 0;
        if (conflito(p))
            cor = 0xC02020;
        else if (fixo[p])
            cor = COR_TEXTO;
        else
            cor = COR_SELECAO;
        g_texto(j, x, y, t, cor);
    }

    for (i = 0; i <= 9; i++) {
        x = GX + i * CEL;
        y = GY + i * CEL;
        cor = (i % 3 == 0) ? COR_TEXTO : COR_BORDA;
        g_retangulo(j, x, GY, (i % 3 == 0) ? 2 : 1, 9 * CEL + 1, cor);
        g_retangulo(j, GX, y, 9 * CEL + 1, (i % 3 == 0) ? 2 : 1, cor);
    }

    if (venceu)
        g_texto(j, 20, GY + 9 * CEL + 18, "Voce venceu!  R recomeca  Q sai",
                COR_DESTAQUE);
    else
        g_texto(j, 20, GY + 9 * CEL + 18, "Preencha todas as casas sem repetir",
                COR_TEXTO_FRACO);

    jan_mostrar(j);
}

static void colocar(int v)
{
    if (fixo[sel]) return;
    tab[sel] = v;
    venceu = terminou();
}

int main(int argc, char **argv)
{
    evento_t e;
    int p, l, c;

    (void)argc;
    (void)argv;
    preparar();
    j = jan_abrir("Sudoku", LARG, ALT);
    if (!j) return 1;
    desenhar();

    for (;;) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) break;

        if (e.tipo == EV_CLIQUE && (e.botoes & 1)) {
            c = (e.x - GX) / CEL;
            l = (e.y - GY) / CEL;
            if (e.x >= GX && e.y >= GY && c >= 0 && c < 9 && l >= 0 && l < 9)
                sel = l * 9 + c;
        } else if (e.tipo == EV_TECLA) {
            if (e.tecla == TECLA_ESQUERDA && sel % 9 > 0) sel--;
            else if (e.tecla == TECLA_DIREITA && sel % 9 < 8) sel++;
            else if (e.tecla == TECLA_CIMA && sel / 9 > 0) sel -= 9;
            else if (e.tecla == TECLA_BAIXO && sel / 9 < 8) sel += 9;
            else if (e.tecla >= '1' && e.tecla <= '9') colocar((int)(e.tecla - '0'));
            else if (e.tecla == '0' || e.tecla == '\b' || e.tecla == TECLA_DELETE)
                colocar(0);
            else if (e.tecla == 'r' || e.tecla == 'R') {
                for (p = 0; p < 81; p++) tab[p] = inicial[p];
                sel = 0;
                venceu = 0;
            } else if (e.tecla == 'q' || e.tecla == 'Q' || e.tecla == TECLA_ESC) {
                break;
            }
        }
        desenhar();
    }

    jan_fechar(j);
    return 0;
}
