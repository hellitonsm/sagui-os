/* terminal - o Terminal em janela (H24).
 *
 * Abre uma janela e roda o sagsh dentro dela, ligado por dois pipes:
 *   teclas da janela  -> pipe de entrada -> entrada 0 do sagsh
 *   saída 1 e 2 do sagsh -> pipe de saída -> desenhado na janela
 * As teclas vão como o terminal do kernel entrega no modo bruto (caracteres
 * Latin-1; setas e afins como ESC + letra), então o sagsh funciona igual,
 * com histórico, Tab e edição. Na saída, o Terminal entende as mesmas
 * sequências do terminal do kernel: ESC [ D / C / K / J.
 *
 * Tudo o que o shell escreve também vai para a saída do próprio Terminal
 * (que, com a área de trabalho no ar, é a serial): é o que os testes leem. */
#include "sagui.h"
#include "gui.h"

#define COLS 94
#define LINS 28
#define MARGEM 4
#define LARG (COLS * LETRA_L + 2 * MARGEM)
#define ALT  (LINS * LETRA_A + 2 * MARGEM)
#define COR_FUNDO_T 0x12161E
#define COR_LETRA   0xD8DDE2

static jan_t *j;
static char grade[LINS][COLS];
static unsigned char cor_grade[LINS][COLS];   /* cor ANSI de cada célula */
static unsigned char cor_letra = 7;           /* 0..15, na ordem ANSI */
static int col, lin, estado_esc;
static char esc_buf[16];
static int esc_len, negrito;
static int ent_escrita;   /* ponta de escrita do pipe de entrada do shell */
static int suj_l0 = LINS, suj_l1 = -1;   /* linhas a redesenhar */

/* As 16 cores ANSI (0..7 normais, 8..15 claras) no tom do terminal. */
static const uint32_t paleta[16] = {
    0x12161E, 0xC82D2D, 0x28A046, 0xB4781E,
    0x2850C8, 0xA03CAA, 0x28A0AA, COR_LETRA,
    0x6E737D, 0xFF6E6E, 0x6EE678, 0xFFDC3C,
    0x6496FF, 0xF078F0, 0x6EE6E6, 0xFFFFFF,
};

static void sujar(int l) { if (l < suj_l0) suj_l0 = l; if (l > suj_l1) suj_l1 = l; }

static void desenhar_linha(int l)
{
    int y = MARGEM + l * LETRA_A;
    g_retangulo(j, 0, y, LARG, LETRA_A, COR_FUNDO_T);
    /* Desenha por trechos da mesma cor, para não perder o colorido do grep. */
    for (int i = 0; i < COLS; ) {
        int k = i + 1;
        while (k < COLS && cor_grade[l][k] == cor_grade[l][i]) k++;
        g_texto_n(j, MARGEM + i * LETRA_L, y, &grade[l][i], k - i,
                  paleta[cor_grade[l][i] & 15]);
        i = k;
    }
    if (l == lin && col < COLS) {   /* cursor: bloco com a letra em cor invertida */
        g_retangulo(j, MARGEM + col * LETRA_L, y, LETRA_L, LETRA_A, COR_LETRA);
        char c = grade[l][col];
        g_texto_n(j, MARGEM + col * LETRA_L, y, &c, 1, COR_FUNDO_T);
    }
}

static void mostrar(void)
{
    if (suj_l1 < 0) return;
    for (int l = suj_l0; l <= suj_l1; l++) desenhar_linha(l);
    jan_mostrar_area(j, 0, MARGEM + suj_l0 * LETRA_A, LARG, (suj_l1 - suj_l0 + 1) * LETRA_A);
    suj_l0 = LINS; suj_l1 = -1;
}

static void limpar(void)
{
    memset(grade, ' ', sizeof grade);
    memset(cor_grade, cor_letra, sizeof cor_grade);
    col = lin = 0;
    for (int l = 0; l < LINS; l++) sujar(l);
}

static void rolar(void)
{
    memmove(grade[0], grade[1], sizeof grade - COLS);
    memmove(cor_grade[0], cor_grade[1], sizeof cor_grade - COLS);
    memset(grade[LINS - 1], ' ', COLS);
    memset(cor_grade[LINS - 1], cor_letra, COLS);
    lin = LINS - 1;
    for (int l = 0; l < LINS; l++) sujar(l);
}

static int sgr_valor(int *pos)
{
    int v = 0;
    while (esc_buf[*pos] >= '0' && esc_buf[*pos] <= '9') {
        v = v * 10 + (esc_buf[*pos] - '0');
        (*pos)++;
    }
    return v;
}

/* ESC [ ... m: só a cor da letra interessa; o fundo é da janela. */
static void aplicar_sgr(void)
{
    int pos = 0;
    for (;;) {
        int v = sgr_valor(&pos);
        if (v == 0) { cor_letra = 7; negrito = 0; }
        else if (v == 1) negrito = 1;
        else if (v == 22) negrito = 0;
        else if (v >= 30 && v <= 37)
            cor_letra = (unsigned char)(v - 30 + (negrito ? 8 : 0));
        else if (v == 39) cor_letra = 7;
        else if (v >= 90 && v <= 97) cor_letra = (unsigned char)(v - 90 + 8);
        if (esc_buf[pos] != ';') break;
        pos++;
    }
}

static void receber(char c)
{
    sujar(lin);
    if (estado_esc == 1) {
        estado_esc = c == '[' ? 2 : 0;
        if (estado_esc == 2) esc_len = 0;
        return;
    }
    if (estado_esc == 2) {
        if ((c >= '0' && c <= '9') || c == ';') {
            if (esc_len < (int)sizeof esc_buf - 1) esc_buf[esc_len++] = c;
            return;
        }
        estado_esc = 0;
        esc_buf[esc_len] = 0;
        if (c == 'm') aplicar_sgr();
        else if (c == 'D' && col > 0) col--;
        else if (c == 'C' && col < COLS - 1) col++;
        else if (c == 'K') {
            memset(&grade[lin][col], ' ', (size_t)(COLS - col));
            memset(&cor_grade[lin][col], cor_letra, (size_t)(COLS - col));
        }
        else if (c == 'J') limpar();
        return;
    }
    if (c == 27) { estado_esc = 1; return; }
    if (c == '\n') { col = 0; lin++; }
    else if (c == '\r') col = 0;
    else if (c == '\t') col = (col + 8) & ~7;
    else if (c == '\b') {
        if (col > 0) {
            grade[lin][--col] = ' ';
            cor_grade[lin][col] = cor_letra;
        }
    } else {
        grade[lin][col] = c;
        cor_grade[lin][col] = cor_letra;
        col++;
    }
    if (col >= COLS) { col = 0; lin++; }
    if (lin >= LINS) rolar();
    sujar(lin);
}

/* Tecla da janela -> bytes para o shell, no formato do modo bruto. */
static void enviar_tecla(unsigned t)
{
    char b[2];
    int n = 1;
    if (t < 0x100) b[0] = (char)t;
    else {
        b[0] = 27;
        switch (t) {
        case TECLA_CIMA: b[1] = TECLA_B_CIMA; break;
        case TECLA_BAIXO: b[1] = TECLA_B_BAIXO; break;
        case TECLA_ESQUERDA: b[1] = TECLA_B_ESQUERDA; break;
        case TECLA_DIREITA: b[1] = TECLA_B_DIREITA; break;
        case TECLA_HOME: b[1] = TECLA_B_HOME; break;
        case TECLA_END: b[1] = TECLA_B_END; break;
        case TECLA_DELETE: b[1] = TECLA_B_DELETE; break;
        case TECLA_PGUP: b[1] = TECLA_B_PGUP; break;
        case TECLA_PGDN: b[1] = TECLA_B_PGDN; break;
        case TECLA_ESC: b[1] = 27; break;
        default: return;
        }
        n = 2;
    }
    escrever_tudo(ent_escrita, b, (unsigned)n);
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    j = jan_abrir("Terminal", LARG, ALT);
    if (!j) return 1;
    g_retangulo(j, 0, 0, LARG, ALT, COR_FUNDO_T);   /* margens também escuras */
    limpar();
    mostrar();

    int entrada[2], saida[2];
    if (criar_pipe(entrada) < 0 || criar_pipe(saida) < 0) return 1;
    /* O shell nasce com o pipe de entrada como 0 e o de saída como 1 e 2. */
    int g0 = duplicar(ENTRADA, 13), g1 = duplicar(SAIDA, 14), g2 = duplicar(ERROS, 15);
    duplicar(entrada[0], ENTRADA);
    duplicar(saida[1], SAIDA);
    duplicar(saida[1], ERROS);
    char *args[] = { "sagsh", NULL };
    int shell = executar("/bin/sagsh", args);
    duplicar(g0, ENTRADA); duplicar(g1, SAIDA); duplicar(g2, ERROS);
    fechar(g0); fechar(g1); fechar(g2);
    fechar(entrada[0]);
    fechar(saida[1]);
    ent_escrita = entrada[1];
    if (shell < 0) { imprimir("terminal: não consegui abrir o sagsh (%s)\n", nome_erro(shell)); return 1; }

    char buf[512];
    evento_t e;
    for (;;) {
        bool fez = false;
        int d = disponivel(saida[0]);
        if (d == ERRO_PIPE_QUEBRADO) break;              /* o shell terminou (sair) */
        if (d > 0) {
            int n = ler(saida[0], buf, sizeof buf);
            for (int i = 0; i < n; i++) receber(buf[i]);
            if (n > 0) escrever(SAIDA, buf, (unsigned)n);  /* cópia para a serial */
            fez = true;
        }
        while (proximo_evento(j->id, &e, false) == 1) {
            fez = true;
            if (e.tipo == EV_FECHAR) { matar(shell); goto fim; }
            if (e.tipo == EV_TECLA) enviar_tecla(e.tecla);
        }
        mostrar();
        /* O shell terminou? (Não dá para depender só do fim do pipe: um
         * programa gráfico aberto pelo shell herda a saída e a mantém aberta.) */
        int c;
        if (recolher(&c) == shell) { shell = -1; break; }
        if (!fez) dormir(10);
    }
fim:
    if (shell > 0) {
        int c;
        esperar(shell, &c);
    }
    jan_fechar(j);
    return 0;
}
