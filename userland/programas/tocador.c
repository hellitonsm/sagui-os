/* tocador - player de video/audio do SaguiOS.
 *
 * O SaguiOS so tem o alto-falante do PC (B3): nao ha codec, DAC nem MP3/MP4.
 * Por isso o tocador toca dois formatos proprios de texto:
 *   .mus - musica: linha "# titulo: ..." e linhas "freq duracao (rotulo)".
 *          freq 0 = pausa, duracao em ms (10..10000).
 *   .vid - video: quadros desenhados com R/T, separados por "---".
 *          R x y l a c (retangulo), T x y texto (texto). c = 0..7 na paleta.
 * .mp3/.mp4/.avi sao recusados com aviso claro (ver docs/tocador.md).
 *
 * Uso: tocador [--info] [arquivo.mus|arquivo.vid]
 * Sem arquivo: abre a janela com a lista de /musicas e /videos.
 */
#include "sagui.h"
#include "gui.h"

#define MAX_NOTAS 256
#define MAX_LINHA 256

static nota_t notas[MAX_NOTAS];
static char rotulos[MAX_NOTAS][32];
static int n_notas;
static char titulo_atual[96];

static uint32_t cor_vid(int c)
{
    switch (c) {
    case 0: return 0x101418;
    case 1: return 0xFFFFFF;
    case 2: return 0x0B7A3E;
    case 3: return 0x2A6FD8;
    case 4: return 0xFF4030;
    case 5: return 0xFFD040;
    case 6: return 0xB04FE0;
    default: return 0x202428;
    }
}

static bool termina_com(const char *s, const char *suf)
{
    unsigned n = (unsigned)tam_texto(s), m = (unsigned)tam_texto(suf);
    return n >= m && compara_texto(s + n - m, suf) == 0;
}

/* Le arquivo inteiro para buf (ate max-1). Devolve tamanho ou -1. */
static int ler_arquivo(const char *caminho, char *buf, unsigned max)
{
    int fd = abrir(caminho, ABRIR_LER);
    if (fd < 0) return -1;
    unsigned n = 0;
    int r = 0;
    while (n + 1 < max && (r = ler(fd, buf + n, max - 1 - n)) > 0) n += (unsigned)r;
    char extra;
    int excedeu = (n + 1 >= max) ? ler(fd, &extra, 1) : 0;
    fechar(fd);
    if (r < 0 || excedeu != 0) return -1;
    buf[n] = 0;
    return (int)n;
}

static int ler_int(const char *s, int *v)
{
    if (!s || !*s) return 0;
    int neg = 0, n = 0;
    const char *p = s;
    if (*p == '-') { neg = 1; p++; }
    if (!*p) return 0;
    for (; *p; p++) {
        if (*p < '0' || *p > '9') return 0;
        n = n * 10 + (*p - '0');
        if (n > 200000000) return 0;
    }
    *v = neg ? -n : n;
    return 1;
}

/* Proximo campo numerico da linha, a partir de *p. */
static bool proximo_num(char **p, int *v)
{
    char *s = *p;
    while (*s == ' ' || *s == '\t') s++;
    char *e = s;
    while (*e && *e != ' ' && *e != '\t') e++;
    char *depois = *e ? e + 1 : e;
    *e = 0;
    bool ok = ler_int(s, v);
    *p = depois;
    return ok;
}

static void ler_titulo(char *buf)
{
    titulo_atual[0] = 0;
    /* primeira linha "# ..." ou "titulo: ..." (sem truncar o resto) */
    unsigned n = 0;
    while (buf[n] && buf[n] != '\n' && n < sizeof(titulo_atual) - 1) n++;
    memcpy(titulo_atual, buf, n);
    titulo_atual[n] = 0;
    const char *s = titulo_atual;
    if (s[0] == '#') { s++; while (*s == ' ') s++; }
    if (s != titulo_atual) copia_texto(titulo_atual, s, sizeof titulo_atual);
}

static int carregar_mus(const char *caminho)
{
    char *buf = aloca(32768);
    if (!buf) return 1;
    int n = ler_arquivo(caminho, buf, 32768);
    if (n < 0) { libera(buf); imprimir_em(ERROS, "tocador: %s: nao consegui ler\n", caminho); return 1; }
    ler_titulo(buf);
    /* percorre linha a linha */
    char *p = buf;
    n_notas = 0;
    while (*p) {
        char *eol = acha_char(p, '\n');
        if (eol) *eol = 0;
        char *s = p;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || *s == '\r') { p = eol ? eol + 1 : p + tam_texto(p); if (eol) continue; else break; }
        if (*s == 0) { if (!eol) break; p = eol + 1; continue; }
        int f, d;
        char *q = s;
        if (proximo_num(&q, &f) && proximo_num(&q, &d)) {
            if (f < 0 || f > 20000 || d < 10 || d > 10000) {
                imprimir_em(ERROS, "tocador: nota fora da faixa: %d %d\n", f, d);
                libera(buf); return 1;
            }
            if (n_notas < MAX_NOTAS) {
                notas[n_notas].freq = (uint16_t)f;
                notas[n_notas].ms = (uint16_t)d;
                while (*q == ' ' || *q == '\t') q++;
                copia_texto(rotulos[n_notas], q, 32);
                n_notas++;
            }
        }
        if (!eol) break;
        p = eol + 1;
    }
    libera(buf);
    return 0;
}

static int contar_quadros_vid(const char *caminho)
{
    char *buf = aloca(32768);
    if (!buf) return -1;
    int n = ler_arquivo(caminho, buf, 32768);
    if (n < 0) { libera(buf); return -1; }
    int q = 1;
    for (int i = 0; i < n - 2; i++)
        if (buf[i] == '-' && buf[i+1] == '-' && buf[i+2] == '-') q++;
    libera(buf);
    return q;
}

static int tocar_mus(const char *caminho)
{
    if (carregar_mus(caminho) != 0) return 1;
    if (n_notas == 0) { imprimir_em(ERROS, "tocador: %s sem notas\n", caminho); return 1; }
    imprimir("tocador: tocando %s (%d notas)\n", titulo_atual[0] ? titulo_atual : caminho, n_notas);
    registrar("tocador: tocando %s (%d notas)\n", caminho, n_notas);
    int r = tocar(notas, n_notas, BIP_SUBSTITUIR);
    if (r < 0) { imprimir_em(ERROS, "tocador: som falhou (%d)\n", r); return 1; }
    /* espera o fim da musica para o teste acompanhar na serial */
    unsigned total = 0;
    for (int i = 0; i < n_notas; i++) total += notas[i].ms;
    dormir(total);
    imprimir("tocador: terminou %s\n", caminho);
    registrar("tocador: terminou %s\n", caminho);
    return 0;
}

/* ---- janela ---- */
#define TLARG 640
#define TALT 480
#define TEL_Y 80
#define TEL_A 300

static jan_t *j;
static char arquivo_atual[160];
static int quadros_vid;
static char linhas_vid[64][MAX_LINHA];
static int n_linhas_vid;

static void desenhar_tela_vazia(void)
{
    g_retangulo(j, 8, TEL_Y, TLARG - 16, TEL_A, cor_vid(0));
    g_moldura(j, 8, TEL_Y, TLARG - 16, TEL_A, COR_BORDA);
    rotulo(j, 24, TEL_Y + 12, "Abra um .mus ou .vid para tocar.", COR_TEXTO_FRACO);
    jan_mostrar_area(j, 8, TEL_Y, TLARG - 16, TEL_A);
}

static void desenhar_quadro(int q)
{
    g_retangulo(j, 8, TEL_Y, TLARG - 16, TEL_A, cor_vid(0));
    int x0 = 8, y0 = TEL_Y;
    for (int i = 0; i < n_linhas_vid; i++) {
        char *s = linhas_vid[i];
        if (s[0] == 'R' || s[0] == 'r') {
            char *p = s + 1; int x, y, l, a, c;
            if (proximo_num(&p, &x) && proximo_num(&p, &y) && proximo_num(&p, &l) && proximo_num(&p, &a) && proximo_num(&p, &c))
                g_retangulo(j, x0 + x, y0 + y, l, a, cor_vid(c));
        } else if (s[0] == 'T' || s[0] == 't') {
            char *p = s + 1; int x, y;
            if (proximo_num(&p, &x) && proximo_num(&p, &y)) {
                while (*p == ' ' || *p == '\t') p++;
                g_texto(j, x0 + x, y0 + y, p, COR_BRANCO);
            }
        }
    }
    char m[80];
    formatar_texto(m, sizeof m, "quadro %d/%d", q + 1, quadros_vid);
    rotulo(j, 24, TEL_Y + TEL_A - 20, m, COR_TEXTO_FRACO);
    jan_mostrar_area(j, 8, TEL_Y, TLARG - 16, TEL_A);
}

static void mostrar_vid(const char *caminho)
{
    char *buf = aloca(32768);
    if (!buf) return;
    int n = ler_arquivo(caminho, buf, 32768);
    if (n < 0) { libera(buf); return; }
    ler_titulo(buf);
    copia_texto(arquivo_atual, caminho, sizeof arquivo_atual);
    quadros_vid = contar_quadros_vid(caminho);
    /* desenha so o primeiro quadro na janela */
    n_linhas_vid = 0;
    char *p = buf;
    bool dentro = true;
    while (*p && n_linhas_vid < 64) {
        char *eol = acha_char(p, '\n');
        if (eol) *eol = 0;
        if (compara_texto(p, "---") == 0) break;
        if (p[0] == 'R' || p[0] == 'T') { copia_texto(linhas_vid[n_linhas_vid], p, MAX_LINHA); n_linhas_vid++; }
        if (!eol) break;
        p = eol + 1;
    }
    (void)dentro;
    desenhar_quadro(0);
    libera(buf);
    char t[128];
    formatar_texto(t, sizeof t, "Tocador - %s", titulo_atual[0] ? titulo_atual : caminho);
    titulo_janela(j->id, t);
    registrar("tocador: video %s (%d quadros)\n", caminho, quadros_vid);
}

static char itens[32][64];
static int qtd_itens;

static void carregar_lista(void)
{
    entrada_pasta_t e;
    qtd_itens = 0;
    const char *pastas[2] = { "/musicas", "/videos" };
    for (int k = 0; k < 2 && qtd_itens < 32; k++) {
        for (int i = 0; listar(pastas[k], i, &e) == 1 && qtd_itens < 32; i++) {
            if (e.tipo == TIPO_PASTA) continue;
            const char *nm = e.nome;
            if (termina_com(nm, ".mus") || termina_com(nm, ".vid")) {
                char tmp[80];
                copia_texto(tmp, pastas[k], sizeof tmp);
                junta_texto(tmp, "/", sizeof tmp);
                junta_texto(tmp, nm, sizeof tmp);
                copia_texto(itens[qtd_itens++], tmp, 64);
            }
        }
    }
}

int principal(int argc, char **argv)
{
    bool info = false;
    const char *arquivo = NULL;
    for (int i = 1; i < argc; i++) {
        if (compara_texto(argv[i], "--info") == 0) info = true;
        else if (compara_texto(argv[i], "-h") == 0 || compara_texto(argv[i], "--ajuda") == 0) {
            imprimir("uso: tocador [--info] [arquivo.mus|arquivo.vid]\n");
            imprimir("sem arquivo: abre a janela com /musicas e /videos.\n");
            return 0;
        } else arquivo = argv[i];
    }
    if (arquivo) {
        if (termina_com(arquivo, ".mp3") || termina_com(arquivo, ".mp4") || termina_com(arquivo, ".avi")) {
            imprimir("tocador: formato nao suportado (%s)\n", arquivo);
            imprimir_em(ERROS, "tocador: use .mus/.vid (sem codec/DAC no hardware)\n");
            return 1;
        }
        if (info) {
            if (termina_com(arquivo, ".mus")) {
                if (carregar_mus(arquivo) != 0) return 1;
                imprimir("tocador: musica \"%s\" %d notas\n", titulo_atual, n_notas);
                registrar("tocador: info musica %s %d notas\n", arquivo, n_notas);
                return n_notas > 0 ? 0 : 1;
            }
            if (termina_com(arquivo, ".vid")) {
                int q = contar_quadros_vid(arquivo);
                if (q < 0) { imprimir_em(ERROS, "tocador: nao consegui ler %s\n", arquivo); return 1; }
                imprimir("tocador: video %s %d quadros\n", arquivo, q);
                registrar("tocador: info video %s %d quadros\n", arquivo, q);
                return 0;
            }
            imprimir_em(ERROS, "tocador: use .mus ou .vid\n");
            return 1;
        }
        if (termina_com(arquivo, ".mus")) return tocar_mus(arquivo);
        if (termina_com(arquivo, ".vid")) {
            int q = contar_quadros_vid(arquivo);
            if (q < 0) { imprimir_em(ERROS, "tocador: nao consegui ler %s\n", arquivo); return 1; }
            imprimir("tocador: video %s %d quadros\n", arquivo, q);
            registrar("tocador: video %s (%d quadros)\n", arquivo, q);
            imprimir("tocador: abra a janela para ver o video\n");
            return 0;
        }
        imprimir_em(ERROS, "tocador: use .mus ou .vid\n");
        return 1;
    }

    /* janela */
    j = jan_abrir("Tocador", TLARG, TALT);
    if (!j) return 1;
    botao_t bt_tocar = { 8, 8, 90, 26, "Tocar", false };
    botao_t bt_parar = { 106, 8, 90, 26, "Parar", false };
    botao_t bt_abrir = { 204, 8, 90, 26, "Abrir...", false };
    botao_t bt_sair = { 302, 8, 90, 26, "Fechar", false };
    botao_desenhar(j, &bt_tocar); botao_desenhar(j, &bt_parar); botao_desenhar(j, &bt_abrir); botao_desenhar(j, &bt_sair);
    carregar_lista();
    lista_t lista = { 8, 40, TLARG - 16, 36, itens, qtd_itens, qtd_itens ? 0 : -1, 0, NULL };
    lista_desenhar(j, &lista);
    desenhar_tela_vazia();
    char m[120];
    formatar_texto(m, sizeof m, "%d arquivos em /musicas e /videos. Duplo clique para tocar.", qtd_itens);
    rotulo(j, 8, TALT - 40, m, COR_TEXTO_FRACO);
    jan_mostrar(j);
    registrar("tocador: pronto\n");
    evento_t e;
    for (;;) {
        if (proximo_evento(j->id, &e, false) != 1) { dormir(20); continue; }
        if (e.tipo == EV_FECHAR) break;
        if (botao_tratar(j, &bt_sair, &e)) break;
        if (botao_tratar(j, &bt_parar, &e)) { silenciar(); registrar("tocador: parado\n"); copia_texto(arquivo_atual, "", sizeof arquivo_atual); desenhar_tela_vazia(); }
        if (botao_tratar(j, &bt_tocar, &e) && lista.sel >= 0 && lista.sel < qtd_itens) {
            const char *c = itens[lista.sel];
            if (termina_com(c, ".mus")) tocar_mus(c);
            else mostrar_vid(c);
        }
        if (botao_tratar(j, &bt_abrir, &e)) {
            char c[160] = "/musicas/";
            if (pedir_texto("Abrir", "Caminho (.mus/.vid):", c, sizeof c) && c[0]) {
                if (termina_com(c, ".mus")) tocar_mus(c);
                else if (termina_com(c, ".vid")) mostrar_vid(c);
                else mensagem("Tocador", "Use .mus ou .vid.");
            }
        }
        int r = lista_tratar(j, &lista, &e);
        if (r == LISTA_ATIVOU && lista.sel >= 0 && lista.sel < qtd_itens) {
            const char *c = itens[lista.sel];
            if (termina_com(c, ".mus")) tocar_mus(c);
            else mostrar_vid(c);
        }
    }
    silenciar();
    registrar("tocador: fechado\n");
    jan_fechar(j);
    return 0;
}
