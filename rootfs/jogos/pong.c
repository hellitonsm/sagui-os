/* pong.c - Pong contra o CPU, dentro do SaguiOS.
 *
 *   csagui build -o /tmp/pong /jogos/pong.c
 *   /tmp/pong
 *
 * W/S movem a raquete da esquerda; Q sai. Primeiro a 5 pontos vence.
 * Rode no terminal (modo bruto, sem eco). O CPU so move de vez em quando. */
int py_jog, py_cpu, bx, by, bvx, bvy, pontos_jog, pontos_cpu, tick, cpucont;

static void reiniciar_bola(void)
{
    bx = 20;
    by = 3 + aleatorio(8);
    if (aleatorio(2) == 0) bvx = -1; else bvx = 1;
    if (aleatorio(2) == 0) bvy = -1; else bvy = 1;
}

static void desenhar(void)
{
    int row, col;
    limpar_tela();
    texto("  PONG   jogador "); numero(pontos_jog); texto(" x "); numero(pontos_cpu); texto(" cpu"); puts("");
    for (row = 0; row < 14; row = row + 1) {
        for (col = 0; col < 40; col = col + 1) {
            if (row == 0 || row == 14 - 1) { caractere(35); continue; }
            if (col == 1 && row >= py_jog && row <= py_jog + 2) { caractere(124); continue; }
            if (col == 40 - 2 && row >= py_cpu && row <= py_cpu + 2) { caractere(124); continue; }
            if (col == bx && row == by) { caractere(111); continue; }
            caractere(32);
        }
        puts("");
    }
    puts("W/S movem a raquete, Q sai");
}

int main(void)
{
    int c;
    pontos_jog = 0;
    pontos_cpu = 0;
    tick = 0;
    cpucont = 0;
    py_jog = 5;
    py_cpu = 5;
    reiniciar_bola();
    entrada_bruta(1);
    for (;;) {
        dormir_ms(60);
        c = tecla_tentada();
        while (c >= 0) {
            if (c == 'q' || c == 'Q') { entrada_bruta(0); puts("Ate mais!"); return 0; }
            if (c == 'w' || c == 'W') py_jog = py_jog - 1;
            if (c == 's' || c == 'S') py_jog = py_jog + 1;
            c = tecla_tentada();
        }
        if (py_jog < 1) py_jog = 1;
        if (py_jog > 14 - 4) py_jog = 14 - 4;
        cpucont = cpucont + 1;
        if (cpucont >= 3) {
            cpucont = 0;
            if (by < py_cpu) py_cpu = py_cpu - 1;
            if (by > py_cpu + 2) py_cpu = py_cpu + 1;
        }
        if (py_cpu < 1) py_cpu = 1;
        if (py_cpu > 14 - 4) py_cpu = 14 - 4;
        tick = tick + 1;
        if (tick >= 2) {
            tick = 0;
            bx = bx + bvx;
            by = by + bvy;
            if (by <= 1) { by = 1; bvy = 0 - bvy; }
            if (by >= 14 - 2) { by = 14 - 2; bvy = 0 - bvy; }
            if (bx == 2 && bvx < 0 && by >= py_jog && by <= py_jog + 2) { bvx = 0 - bvx; bx = 3; }
            if (bx == 40 - 3 && bvx > 0 && by >= py_cpu && by <= py_cpu + 2) { bvx = 0 - bvx; bx = 40 - 4; }
            if (bx <= 0) { pontos_cpu = pontos_cpu + 1; reiniciar_bola(); }
            if (bx >= 40 - 1) { pontos_jog = pontos_jog + 1; reiniciar_bola(); }
        }
        desenhar();
        if (pontos_jog >= 5 || pontos_cpu >= 5) {
            entrada_bruta(0);
            limpar_tela();
            if (pontos_jog >= 5) puts("Voce venceu!"); else puts("O CPU venceu!");
            return 0;
        }
    }
}


