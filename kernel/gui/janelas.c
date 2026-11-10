/* janelas.c - o gerenciador de janelas (H21). Ver D021.
 *
 * O kernel guarda a lista de janelas em ordem de empilhamento (a última é a
 * de cima e tem o foco), desenha as molduras e entrega os eventos. O
 * conteúdo de cada janela é desenhado pelo próprio programa, no anel 3,
 * em páginas que o kernel compartilha com ele.
 *
 * Redesenhar = recompor só o retângulo que mudou: fundo pronto da área de
 * trabalho, janelas de baixo para cima (moldura + conteúdo), barra superior
 * por último. Tudo recortado no retângulo, num buffer de trás (vídeo).
 *
 * Uma tarefa do kernel acorda a cada tique, consome as filas do mouse e do
 * teclado, arrasta, foca e fecha janelas, e entrega os eventos. Ela e as
 * chamadas de sistema mexem nas mesmas listas; por isso a tarefa trabalha
 * com as interrupções desligadas (as chamadas já rodam assim). */
#include "gui.h"
#include "abi.h"
#include "console.h"
#include "memoria.h"
#include "mouse.h"
#include "paginacao.h"
#include "processo.h"
#include "relogio.h"
#include "sistema.h"
#include "teclado.h"
#include "texto.h"
#include "video.h"
#include "disco.h"

/* energia.c: o menu SaguiOS desliga e reinicia a máquina (D038). */
SEM_RETORNO void maquina_reiniciar(void);
SEM_RETORNO void maquina_desligar(void);

#define MAX_JANELAS     32
#define FILA_EVENTOS    64
#define BARRA_TITULO    24
#define BORDA           2
#define JANELAS_USUARIO 0xA0000000u   /* onde os pixels aparecem no espaço do programa */
#define ESPACO_JANELA   0x00400000u   /* 4 MB por janela */
#define DUPLO_CLIQUE    45            /* tiques (450 ms) */

typedef struct {
    bool       usada;
    int        id;
    int        pid;
    int        x, y;          /* canto da moldura */
    int        larg, alt;     /* área de conteúdo */
    char       titulo[48];
    uint32_t  *px;            /* pixels (físico = virtual no kernel) */
    uint32_t   paginas;
    uint32_t   virt;          /* endereço dos pixels no espaço do programa */
    evento_t   fila[FILA_EVENTOS];
    uint32_t   ini, fim;
} janela_t;

static janela_t janelas[MAX_JANELAS];
static int ordem[MAX_JANELAS], qtd;   /* índices em janelas[], de baixo para cima */
static int proximo_id = 1;
static bool ativa;
static superficie_t tela;
static int suj_x0, suj_y0, suj_x1, suj_y1;   /* retângulo a recompor */

/* Estado do mouse no gerenciador. */
static int arrastando = -1, arr_dx, arr_dy;   /* janela sendo arrastada e onde foi pega */
static int captura = -1;                       /* janela que recebeu o último clique */
static uint32_t ult_clique_tique;
static int ult_clique_x, ult_clique_y, ult_clique_alvo = -999;

bool gui_ativa(void) { return ativa; }

/* ------------------------------------------------------------ notificação (H34) */
#define AVISO_L 420
#define AVISO_A 62
#define AVISO_COLUNAS 49   /* letras por linha dentro da caixa */
static char aviso_texto[96];
static uint32_t aviso_ate;   /* tique em que some; 0 = sem aviso */

static void aviso_retangulo(int *x, int *y) { *x = (int)video_largura() - AVISO_L - 12; *y = BARRA_SUPERIOR + 10; }

void gui_aviso(const char *texto)
{
    txt_copia_n(aviso_texto, texto, sizeof aviso_texto);
    aviso_ate = relogio_tiques() + 5 * TIQUES_POR_SEGUNDO;
    int x, y;
    aviso_retangulo(&x, &y);
    gui_redesenhar(x - 2, y - 2, AVISO_L + 4, AVISO_A + 4);
    klog("[GUI] notificacao: %s\n", texto);
}

static void desenhar_aviso(superficie_t *t)
{
    if (!aviso_ate) return;
    int x, y;
    aviso_retangulo(&x, &y);
    sup_retangulo(t, x, y, AVISO_L, AVISO_A, 0x18202A);
    sup_moldura(t, x, y, AVISO_L, AVISO_A, 0xFFDF4A);
    sup_retangulo(t, x, y, 6, AVISO_A, 0xFFDF4A);
    sup_texto(t, x + 16, y + 6, "SaguiOS", 0xFFDF4A);
    /* Até duas linhas, quebrando no último espaço que couber. */
    const char *p = aviso_texto;
    for (int l = 0; l < 2 && *p; l++) {
        int n = (int)txt_tamanho(p);
        if (n > AVISO_COLUNAS) {
            n = AVISO_COLUNAS;
            while (n > 0 && p[n] != ' ') n--;
            if (n == 0) n = AVISO_COLUNAS;
        }
        char linha[AVISO_COLUNAS + 1];
        txt_copia_n(linha, p, (size_t)n + 1);
        sup_texto(t, x + 16, y + 24 + l * 17, linha, 0xFFFFFF);
        p += n;
        while (*p == ' ') p++;
    }
}

/* ------------------------------------------------------------ menu SaguiOS (D038) */

/* O menu pendurado no botão "SaguiOS" da barra: desligar e reiniciar. É
 * desenhado por cima de tudo, cobrindo a barra e o começo da área de
 * trabalho, como o aviso. */
#define MENU_L      150
#define MENU_ITENS  2
#define MENU_ITEM_A 26

static bool menu_aberto;

static void desenhar_menu(superficie_t *t)
{
    if (!menu_aberto) return;
    int a = MENU_ITENS * MENU_ITEM_A;
    /* Tarja verde embaixo do nome: o botão está aberto. */
    sup_retangulo(t, 0, BARRA_SUPERIOR - 3, BARRA_BOTAO_L, 3, 0x0B7A3E);
    sup_retangulo(t, 0, BARRA_SUPERIOR, MENU_L, a, 0x18202A);
    sup_moldura(t, 0, BARRA_SUPERIOR, MENU_L, a, 0x0B7A3E);
    sup_retangulo(t, 0, BARRA_SUPERIOR, 6, a, 0x0B7A3E);
    static const char *itens[MENU_ITENS] = { "Desligar", "Reiniciar" };
    for (int i = 0; i < MENU_ITENS; i++)
        sup_texto(t, 20, BARRA_SUPERIOR + i * MENU_ITEM_A + 6, itens[i], 0xFFFFFF);
}

static void redesenhar_menu(void)
{
    gui_redesenhar(0, 0, MENU_L, BARRA_SUPERIOR + MENU_ITENS * MENU_ITEM_A);
    gui_mostrar();
}

static void abrir_menu(void)
{
    menu_aberto = true;
    redesenhar_menu();
    klog("[GUI] menu SaguiOS aberto\n");
}

static void fechar_menu(void)
{
    if (!menu_aberto) return;
    menu_aberto = false;
    redesenhar_menu();
    klog("[GUI] menu SaguiOS fechado\n");
}

/* Item do menu sob o ponto, ou -1 se o clique caiu fora dele. */
static int menu_item_em(int x, int y)
{
    if (x < 0 || x >= MENU_L || y < BARRA_SUPERIOR || y >= BARRA_SUPERIOR + MENU_ITENS * MENU_ITEM_A)
        return -1;
    return (y - BARRA_SUPERIOR) / MENU_ITEM_A;
}

/* Um aperto de botão que começa na barra superior: no botão "SaguiOS" abre o
 * menu, numa faixa do alternador traz a janela para cima. Com o menu
 * aberto, o clique não passa para as janelas nem para os ícones: o primeiro
 * clique só fecha (e um item escolhido com o botão esquerdo é executado). */
static bool clicar_barra(evento_mouse_t *m)
{
    int x = m->x, y = m->y;
    if (menu_aberto) {
        int item = menu_item_em(x, y);
        fechar_menu();
        if (item == 0 && (m->mudaram & BOTAO_ESQUERDO)) maquina_desligar();   /* não volta */
        else if (item == 1 && (m->mudaram & BOTAO_ESQUERDO)) maquina_reiniciar();
        return true;
    }
    if (y >= BARRA_SUPERIOR) return false;
    if (x < BARRA_BOTAO_L && (m->mudaram & BOTAO_ESQUERDO)) { abrir_menu(); return true; }
    if (m->mudaram & BOTAO_ESQUERDO) {
        int id = area_barra_janela_em(x, y);
        if (id) { janela_focar(id); return true; }
    }
    return false;   /* resto da barra: o clique vai pelo caminho de sempre */
}

static int moldura_l(janela_t *j) { return j->larg + 2 * BORDA; }
static int moldura_a(janela_t *j) { return j->alt + BARRA_TITULO + BORDA; }

void gui_redesenhar(int x, int y, int l, int a)
{
    if (l <= 0 || a <= 0) return;
    if (suj_x0 >= suj_x1) { suj_x0 = x; suj_y0 = y; suj_x1 = x + l; suj_y1 = y + a; return; }
    if (x < suj_x0) suj_x0 = x;
    if (y < suj_y0) suj_y0 = y;
    if (x + l > suj_x1) suj_x1 = x + l;
    if (y + a > suj_y1) suj_y1 = y + a;
}

/* Inclui o contorno escuro de 1 pixel, desenhado por fora da moldura (sem
 * isso, arrastar deixava rastros). */
static void redesenhar_janela(janela_t *j)
{
    gui_redesenhar(j->x - 1, j->y - 1, moldura_l(j) + 2, moldura_a(j) + 2);
}

/* A barra lista as janelas abertas: qualquer mudança nelas suja a barra. */
static void barra_sujar(void)
{
    gui_redesenhar(0, 0, (int)video_largura(), BARRA_SUPERIOR);
}

static janela_t *focada(void)
{
    return qtd ? &janelas[ordem[qtd - 1]] : NULL;
}

static void desenhar_moldura(janela_t *j, bool foco)
{
    int x = j->x, y = j->y, l = moldura_l(j), a = moldura_a(j);
    uint32_t barra = foco ? 0x0B7A3E : 0x5A6470;
    sup_retangulo(&tela, x, y, l, BARRA_TITULO, barra);
    sup_retangulo(&tela, x, y + BARRA_TITULO, BORDA, a - BARRA_TITULO, barra);
    sup_retangulo(&tela, x + l - BORDA, y + BARRA_TITULO, BORDA, a - BARRA_TITULO, barra);
    sup_retangulo(&tela, x, y + a - BORDA, l, BORDA, barra);
    sup_moldura(&tela, x - 1, y - 1, l + 2, a + 2, 0x101010);
    sup_texto(&tela, x + 10, y + 4, j->titulo, foco ? 0xFFFFFF : 0xD0D4D8);
    /* Botão de fechar: um quadrado vermelho com um X. */
    int bx = x + l - BORDA - 20, by = y + 4;
    sup_retangulo(&tela, bx, by, 16, 16, foco ? 0xD03C32 : 0x8A5A56);
    for (int k = 3; k < 13; k++) {
        sup_retangulo(&tela, bx + k, by + k, 2, 1, 0xFFFFFF);
        sup_retangulo(&tela, bx + 15 - k - 1, by + k, 2, 1, 0xFFFFFF);
    }
}

/* Recompõe o retângulo sujo e mostra. */
void gui_mostrar(void)
{
    if (!ativa || suj_x0 >= suj_x1) return;
    int x = suj_x0, y = suj_y0, l = suj_x1 - suj_x0, a = suj_y1 - suj_y0;
    suj_x0 = suj_x1 = 0;
    sup_recorte(&tela, x, y, l, a);
    if (tela.cx0 >= tela.cx1 || tela.cy0 >= tela.cy1) return;
    area_fundo(&tela);
    for (int k = 0; k < qtd; k++) {
        janela_t *j = &janelas[ordem[k]];
        desenhar_moldura(j, k == qtd - 1);
        sup_copiar(&tela, j->x + BORDA, j->y + BARRA_TITULO, j->px, j->larg, j->alt, j->larg);
    }
    desenhar_aviso(&tela);
    if (tela.cy0 < BARRA_SUPERIOR) area_barra(&tela);
    desenhar_menu(&tela);
    video_marcar(tela.cx0, tela.cy0, tela.cx1 - tela.cx0, tela.cy1 - tela.cy0);
    video_mostrar();
}

/* ------------------------------------------------------------ eventos */

static void entregar(janela_t *j, evento_t *e)
{
    e->janela = j->id;
    uint32_t prox = (j->fim + 1) % FILA_EVENTOS;
    if (prox == j->ini) {
        /* Fila cheia: movimentos podem ser jogados fora; o resto não. */
        if (e->tipo == EV_MOVER) return;
        j->ini = (j->ini + 1) % FILA_EVENTOS;
    }
    j->fila[j->fim] = *e;
    j->fim = prox;
    acordar(processo_por_pid(j->pid));
}

static int janela_em(int x, int y)   /* índice em ordem[], de cima para baixo */
{
    for (int k = qtd - 1; k >= 0; k--) {
        janela_t *j = &janelas[ordem[k]];
        if (x >= j->x && x < j->x + moldura_l(j) && y >= j->y && y < j->y + moldura_a(j))
            return k;
    }
    return -1;
}

static void trazer_para_cima(int k)
{
    if (k == qtd - 1) return;
    janela_t *antes = focada();
    int i = ordem[k];
    for (; k < qtd - 1; k++) ordem[k] = ordem[k + 1];
    ordem[qtd - 1] = i;
    if (antes) redesenhar_janela(antes);
    redesenhar_janela(&janelas[i]);
    barra_sujar();   /* o foco trocou de janela: o alternador acompanha */
}

/* ------------------------------------------------------------ janelas abertas na barra (D038) */

/* Para o alternador: a janela i contando de cima (a 0 é a de cima, em foco).
 * Copia o título e devolve o id, ou 0 quando acabaram. */
int janela_listar(int i, char *titulo, uint32_t max)
{
    if (i < 0 || i >= qtd) return 0;
    janela_t *j = &janelas[ordem[qtd - 1 - i]];
    if (titulo && max) txt_copia_n(titulo, j->titulo, max);
    return j->id;
}

int janela_focada_id(void)
{
    janela_t *j = focada();
    return j ? j->id : 0;
}

void janela_focar(int id)
{
    for (int k = 0; k < qtd; k++)
        if (janelas[ordem[k]].id == id) {
            klog("[GUI] alternador traz a janela %d (%s) para cima\n", id, janelas[ordem[k]].titulo);
            trazer_para_cima(k);
            return;
        }
}

static void tratar_mouse(evento_mouse_t *m)
{
    bool apertou = (m->mudaram & m->botoes) != 0;
    bool soltou = (m->mudaram & ~m->botoes) != 0;

    if (arrastando >= 0) {
        janela_t *j = &janelas[arrastando];
        if (!j->usada || !(m->botoes & BOTAO_ESQUERDO)) {
            if (j->usada) klog("[GUI] janela %d movida para (%d,%d)\n", j->id, j->x, j->y);
            arrastando = -1;
            return;
        }
        int nx = m->x - arr_dx, ny = m->y - arr_dy;
        if (ny < BARRA_SUPERIOR) ny = BARRA_SUPERIOR;
        if (ny > (int)video_altura() - BARRA_TITULO) ny = (int)video_altura() - BARRA_TITULO;
        if (nx > (int)video_largura() - 40) nx = (int)video_largura() - 40;
        if (nx < 40 - moldura_l(j)) nx = 40 - moldura_l(j);
        if (nx != j->x || ny != j->y) {
            redesenhar_janela(j);
            j->x = nx; j->y = ny;
            redesenhar_janela(j);
        }
        return;
    }

    if (apertou) {
        if (clicar_barra(m)) return;
        int k = janela_em(m->x, m->y);
        if (k < 0) {
            /* Na área de trabalho: duplo clique num ícone abre o programa. */
            int ic = area_icone_em(m->x, m->y);
            uint32_t agora = relogio_tiques();
            bool duplo = ic >= 0 && ult_clique_alvo == 1000 + ic && agora - ult_clique_tique < DUPLO_CLIQUE;
            ult_clique_alvo = ic >= 0 ? 1000 + ic : -999;
            ult_clique_tique = agora;
            if (duplo && (m->mudaram & BOTAO_ESQUERDO)) { area_icone_abrir(ic); ult_clique_alvo = -999; }
            if (ic >= 0 && (m->mudaram & BOTAO_DIREITO)) area_icone_direito(ic);
            captura = -1;
            return;
        }
        trazer_para_cima(k);
        janela_t *j = focada();
        int idx = ordem[qtd - 1];
        int rx = m->x - j->x, ry = m->y - j->y;
        if (ry < BARRA_TITULO) {
            int bx = moldura_l(j) - BORDA - 20;
            if ((m->mudaram & BOTAO_ESQUERDO) && rx >= bx && rx < bx + 16 && ry >= 4 && ry < 20) {
                evento_t e = { .tipo = EV_FECHAR };
                entregar(j, &e);
                klog("[GUI] fechar janela %d (%s)\n", j->id, j->titulo);
            } else if (m->mudaram & BOTAO_ESQUERDO) {
                arrastando = idx;
                arr_dx = rx; arr_dy = ry;
            }
            captura = -1;
            return;
        }
        int cx = m->x - j->x - BORDA, cy = m->y - j->y - BARRA_TITULO;
        if (cx < 0 || cy < 0 || cx >= j->larg || cy >= j->alt) return;   /* na borda */
        uint32_t agora = relogio_tiques();
        int dx = cx - ult_clique_x, dy = cy - ult_clique_y;
        bool duplo = ult_clique_alvo == j->id && agora - ult_clique_tique < DUPLO_CLIQUE &&
                     dx > -5 && dx < 5 && dy > -5 && dy < 5;
        evento_t e = { .tipo = EV_CLIQUE, .x = cx, .y = cy, .botoes = m->mudaram & m->botoes,
                       .cliques = duplo ? 2 : 1, .modificadores = teclado_modificadores() & 3 };
        ult_clique_alvo = duplo ? -999 : j->id;
        ult_clique_tique = agora;
        ult_clique_x = cx; ult_clique_y = cy;
        entregar(j, &e);
        captura = idx;
        return;
    }

    if (captura >= 0 && janelas[captura].usada) {
        janela_t *j = &janelas[captura];
        evento_t e = { .tipo = soltou ? EV_SOLTOU : EV_MOVER, .x = m->x - j->x - BORDA,
                       .y = m->y - j->y - BARRA_TITULO, .botoes = soltou ? m->mudaram : m->botoes };
        entregar(j, &e);
        if (soltou) captura = -1;
        return;
    }
    /* Movimento sem botão: só para a janela com foco, se o mouse estiver nela. */
    janela_t *j = focada();
    if (j) {
        int cx = m->x - j->x - BORDA, cy = m->y - j->y - BARRA_TITULO;
        if (cx >= 0 && cy >= 0 && cx < j->larg && cy < j->alt) {
            evento_t e = { .tipo = EV_MOVER, .x = cx, .y = cy, .botoes = 0 };
            entregar(j, &e);
        }
    }
}

static void tarefa_gerenciador(void *arg)
{
    (void)arg;
    static const char canal = 0;
    for (;;) {
        int_desliga();
        evento_mouse_t m;
        while (mouse_tentar(&m))
            tratar_mouse(&m);
        int t;
        while ((t = teclado_tentar()) >= 0) {
            if (menu_aberto && t == TECLA_ESC) { fechar_menu(); continue; }
            janela_t *j = focada();
            if (!j) continue;
            evento_t e = { .tipo = EV_TECLA, .tecla = (unsigned)t, .modificadores = teclado_modificadores() & 3 };
            entregar(j, &e);
        }
        if (area_relogio_mudou())
            gui_redesenhar(0, 0, (int)video_largura(), BARRA_SUPERIOR);
        /* Pendrive: avisos do kernel viram notificações; o ícone acompanha. */
        int tipo;
        char texto[96];
        while (disco_proximo_aviso(&tipo, texto, sizeof texto))
            gui_aviso(texto);
        disco_t *pd = NULL;
        for (int k = 0; k < MAX_DISCOS && !pd; k++) { disco_t *d = disco_indice(k); if (d && d->presente && !d->de_boot) pd = d; }
        if (area_pendrive(pd != NULL, pd ? (pd->rotulo[0] ? pd->rotulo : "Pendrive") : NULL))
            gui_redesenhar(0, BARRA_SUPERIOR, 140, (int)video_altura() - BARRA_SUPERIOR);
        if (aviso_ate && (int32_t)(relogio_tiques() - aviso_ate) >= 0) {
            aviso_ate = 0;
            int x, y;
            aviso_retangulo(&x, &y);
            gui_redesenhar(x - 2, y - 2, AVISO_L + 4, AVISO_A + 4);
        }
        gui_mostrar();
        /* Dorme até o próximo tique (o escalonador acorda pelo despertador). */
        atual->acordar_em = relogio_tiques() + 1;
        bloquear(&canal);
        ceder();
    }
}

bool gui_iniciar(void)
{
    if (!video_ativo() || !area_iniciar()) return false;
    sup_iniciar(&tela, video_buffer(), (int)video_largura(), (int)video_altura());
    ativa = true;
    gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura());
    gui_mostrar();
    tarefa_kernel("janelas", tarefa_gerenciador, NULL);
    klog("[GUI] area de trabalho pronta\n");
    return true;
}

/* ------------------------------------------------------------ chamadas de sistema */

static janela_t *janela_do_processo(int id)
{
    for (int i = 0; i < MAX_JANELAS; i++)
        if (janelas[i].usada && janelas[i].id == id)
            return janelas[i].pid == atual->pid ? &janelas[i] : NULL;
    return NULL;
}

static void destruir(int i)
{
    janela_t *j = &janelas[i];
    redesenhar_janela(j);
    processo_t *dono = processo_por_pid(j->pid);
    if (dono && dono->usuario && dono->espaco != espaco_kernel())
        for (uint32_t k = 0; k < j->paginas; k++)
            espaco_desmapear(dono->espaco, j->virt + k * PAGINA);
    memfisica_liberar_seguidas((uint32_t)j->px, j->paginas);
    j->usada = false;
    int k = 0;
    while (k < qtd && ordem[k] != i) k++;
    for (; k < qtd - 1; k++) ordem[k] = ordem[k + 1];
    if (k < qtd) qtd--;
    if (arrastando == i) arrastando = -1;
    if (captura == i) captura = -1;
    if (focada()) redesenhar_janela(focada());   /* a nova janela de cima ganha o foco */
    barra_sujar();   /* a faixa dela some do alternador */
}

void janelas_do_processo_fechar(processo_t *p)
{
    for (int i = 0; i < MAX_JANELAS; i++)
        if (janelas[i].usada && janelas[i].pid == p->pid)
            destruir(i);
    gui_mostrar();
}

int32_t sis_criar_janela(uint32_t ptitulo, uint32_t dims, uint32_t ppixels, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    if (!ativa) return ERRO_ACESSO;
    char titulo[48];
    int r = usuario_texto((const char *)ptitulo, titulo, sizeof titulo);
    if (r < 0) return r;
    int l = (int)(dims & 0xFFFF), a = (int)(dims >> 16);
    if (l < 40 || a < 20 || l > JANELA_LARG_MAX || a > JANELA_ALT_MAX) return ERRO_INVALIDO;
    if (!usuario_ok((void *)ppixels, 4, true)) return ERRO_ENDERECO;
    int i = 0;
    while (i < MAX_JANELAS && janelas[i].usada) i++;
    if (i == MAX_JANELAS) return ERRO_LIMITE;
    uint32_t paginas = ((uint32_t)(l * a) * 4 + PAGINA - 1) / PAGINA;
    uint32_t fis = memfisica_alocar_seguidas(paginas);
    if (!fis) return ERRO_SEM_MEMORIA;
    janela_t *j = &janelas[i];
    memset(j, 0, sizeof *j);
    j->usada = true;
    j->id = proximo_id++;
    j->pid = atual->pid;
    j->larg = l; j->alt = a;
    j->px = (uint32_t *)fis;
    j->paginas = paginas;
    j->virt = JANELAS_USUARIO + (uint32_t)i * ESPACO_JANELA;
    txt_copia_n(j->titulo, titulo, sizeof j->titulo);
    for (uint32_t k = 0; k < (uint32_t)(l * a); k++) j->px[k] = 0xF4F4F0;
    for (uint32_t k = 0; k < paginas; k++)
        espaco_mapear(atual->espaco, j->virt + k * PAGINA, fis + k * PAGINA, PG_USUARIO | PG_ESCRITA | PG_EMPRESTADA);
    /* Posição em cascata, sem passar da tela. */
    int n = j->id % 8;
    j->x = 150 + n * 34;
    j->y = BARRA_SUPERIOR + 30 + n * 28;
    if (j->x + moldura_l(j) > (int)video_largura()) j->x = (int)video_largura() - moldura_l(j);
    if (j->y + moldura_a(j) > (int)video_altura()) j->y = (int)video_altura() - moldura_a(j);
    if (j->x < 0) j->x = 0;
    if (j->y < BARRA_SUPERIOR) j->y = BARRA_SUPERIOR;
    janela_t *antes = focada();
    if (antes) redesenhar_janela(antes);
    ordem[qtd++] = i;
    redesenhar_janela(j);
    barra_sujar();   /* a janela nova ganha uma faixa no alternador */
    gui_mostrar();
    *(uint32_t *)ppixels = j->virt;
    klog("[GUI] janela %d criada por %d: \"%s\" %dx%d em (%d,%d)\n", j->id, j->pid, j->titulo, l, a, j->x, j->y);
    return j->id;
}

int32_t sis_fechar_janela(uint32_t id, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    janela_t *j = janela_do_processo((int)id);
    if (!j) return ERRO_INVALIDO;
    destruir((int)(j - janelas));
    gui_mostrar();
    return 0;
}

int32_t sis_atualizar_janela(uint32_t id, uint32_t pret, uint32_t c, uint32_t d, uint32_t e)
{
    (void)c; (void)d; (void)e;
    janela_t *j = janela_do_processo((int)id);
    if (!j) return ERRO_INVALIDO;
    int x = 0, y = 0, l = j->larg, a = j->alt;
    if (pret) {
        if (!usuario_ok((void *)pret, sizeof(retangulo_t), false)) return ERRO_ENDERECO;
        retangulo_t *r = (retangulo_t *)pret;
        x = r->x; y = r->y; l = r->largura; a = r->altura;
        if (x < 0) { l += x; x = 0; }
        if (y < 0) { a += y; y = 0; }
        if (x + l > j->larg) l = j->larg - x;
        if (y + a > j->alt) a = j->alt - y;
    }
    gui_redesenhar(j->x + BORDA + x, j->y + BARRA_TITULO + y, l, a);
    gui_mostrar();
    return 0;
}

int32_t sis_proximo_evento(uint32_t id, uint32_t pev, uint32_t esperar, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    if (!usuario_ok((void *)pev, sizeof(evento_t), true)) return ERRO_ENDERECO;
    bool tem_janela = false;
    for (int i = 0; i < MAX_JANELAS; i++) {
        janela_t *j = &janelas[i];
        if (!j->usada || j->pid != atual->pid || (id && j->id != (int)id)) continue;
        tem_janela = true;
        if (j->ini != j->fim) {
            *(evento_t *)pev = j->fila[j->ini];
            j->ini = (j->ini + 1) % FILA_EVENTOS;
            return 1;
        }
    }
    if (!tem_janela) return ERRO_INVALIDO;
    if (!esperar) return 0;
    bloquear(atual);   /* "entregar" acorda o dono da janela */
    return SIS_REPETIR;
}

/* SIS_TECLAS_SEGURADAS (&mapa[32]): quais teclas estão apertadas agora.
 * Só quem tem a janela em foco recebe o mapa; os outros recebem tudo zerado,
 * para um programa em segundo plano não espiar o que se digita. */
int32_t sis_teclas_seguradas(uint32_t pmapa, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    if (!usuario_ok((void *)pmapa, 32, true)) return ERRO_INVALIDO;
    uint8_t mapa[32];
    memset(mapa, 0, sizeof mapa);
    janela_t *j = focada();
    bool foco = j && j->pid == atual->pid;
    if (foco) teclado_seguradas(mapa);
    memcpy((void *)pmapa, mapa, 32);
    return foco ? 1 : 0;
}

int32_t sis_titulo_janela(uint32_t id, uint32_t ptitulo, uint32_t c, uint32_t d, uint32_t e)
{
    (void)c; (void)d; (void)e;
    janela_t *j = janela_do_processo((int)id);
    if (!j) return ERRO_INVALIDO;
    int r = usuario_texto((const char *)ptitulo, j->titulo, sizeof j->titulo);
    if (r < 0) return r;
    gui_redesenhar(j->x, j->y, moldura_l(j), BARRA_TITULO);
    barra_sujar();   /* o título também aparece no alternador */
    gui_mostrar();
    return 0;
}
