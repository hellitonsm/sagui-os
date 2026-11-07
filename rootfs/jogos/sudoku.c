/* sudoku.c - Sudoku 9x9 compilado e rodado dentro do SaguiOS.
 *
 *   csagui build -o /tmp/sudoku /jogos/sudoku.c
 *   /tmp/sudoku
 *
 * Comando: "linha coluna valor" (ex.: "3 5 7"); valor 0 apaga uma casa;
 * "q" sai. Linha e coluna de 1 a 9. A linha e lida no terminal em modo
 * linha, com eco do proprio SaguiOS. */
int tab[81] = { 5,3,0,0,7,0,0,0,0,
                6,0,0,1,9,5,0,0,0,
                0,9,8,0,0,0,0,6,0,
                8,0,0,0,6,0,0,0,3,
                4,0,0,8,0,3,0,0,1,
                7,0,0,0,2,0,0,0,6,
                0,6,0,0,0,0,2,8,0,
                0,0,0,4,1,9,0,0,5,
                0,0,0,0,8,0,0,7,9 };
int fixo[81];
int linha[16];
int src_pos;

static void preparar_fixos(void)
{
    int i;
    for (i = 0; i < 81; i = i + 1) {
        if (tab[i] != 0) fixo[i] = 1; else fixo[i] = 0;
    }
}

static int pode_por(int pos, int v)
{
    int i, j, l, c, bl, bc;
    l = pos / 9;
    c = pos % 9;
    for (i = 0; i < 9; i = i + 1) {
        if (tab[l * 9 + i] == v) return 0;
        if (tab[i * 9 + c] == v) return 0;
    }
    bl = (l / 3) * 3;
    bc = (c / 3) * 3;
    for (i = 0; i < 3; i = i + 1) {
        for (j = 0; j < 3; j = j + 1) {
            if (tab[(bl + i) * 9 + bc + j] == v) return 0;
        }
    }
    return 1;
}

static int completo(void)
{
    int i;
    for (i = 0; i < 81; i = i + 1) {
        if (tab[i] == 0) return 0;
    }
    return 1;
}

static void mostrar_tabuleiro(void)
{
    int i, j, v;
    limpar_tela();
    puts("    1 2 3   4 5 6   7 8 9");
    puts("  -----------------------");
    for (i = 0; i < 9; i = i + 1) {
        numero(i + 1);
        texto(" | ");
        for (j = 0; j < 9; j = j + 1) {
            v = tab[i * 9 + j];
            if (v == 0) texto(". "); else { numero(v); texto(" "); }
            if (j == 2 || j == 5) texto("| ");
        }
        puts("|");
        if (i == 2 || i == 5) puts("  -----------------------");
    }
}

static int ler_linha(void)
{
    int i, c;
    i = 0;
    for (;;) {
        c = tecla();
        if (c < 0) return -1;
        if (c == 10) break;
        if (i < 14) { linha[i] = c; i = i + 1; }
    }
    linha[i] = 0;
    return i;
}

/* Devolve o proximo numero da linha, -1 se acabou ou -2 se pediu para sair. */
static int proximo_int(void)
{
    int n, d;
    n = -1;
    d = 0;
    if (src_pos > 14) return -1;
    while (src_pos <= 14 && (linha[src_pos] < '0' || linha[src_pos] > '9')) {
        if (linha[src_pos] == 'q') return -2;
        if (linha[src_pos] == 0) return -1;
        src_pos = src_pos + 1;
    }
    while (src_pos <= 14 && linha[src_pos] >= '0' && linha[src_pos] <= '9') {
        if (n < 0) n = 0;
        n = n * 10 + (linha[src_pos] - '0');
        src_pos = src_pos + 1;
        d = d + 1;
    }
    return n;
}

int main(void)
{
    int l, c, v, cmd, pos;
    preparar_fixos();
    for (;;) {
        mostrar_tabuleiro();
        if (completo()) {
            puts("Parabens, voce completou o sudoku!");
            return 0;
        }
        puts("linha coluna valor (ex.: 3 5 7), 0 apaga, q sai");
        if (ler_linha() < 0) { puts("Fim da entrada."); return 0; }
        src_pos = 0;
        cmd = proximo_int();
        if (cmd == -2) { puts("Ate mais!"); return 0; }
        l = cmd;
        c = proximo_int();
        v = proximo_int();
        if (l < 1 || l > 9 || c < 1 || c > 9 || v < 0 || v > 9) {
            puts("Formato: linha coluna valor (ex.: 3 5 7)");
            continue;
        }
        pos = (l - 1) * 9 + (c - 1);
        if (fixo[pos] == 1) { puts("Esse numero faz parte do puzzle."); continue; }
        if (v != 0 && pode_por(pos, v) == 0) {
            puts("Jogada invalida: ja existe na linha, coluna ou bloco.");
            continue;
        }
        tab[pos] = v;
        if (v == 0) puts("Casa apagada."); else puts("Jogada aceita.");
    }
}
