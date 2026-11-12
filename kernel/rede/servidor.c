/* servidor.c - servidor de arquivos pela rede: TCP na porta 2500 (H48).
 *
 * A pilha de rede do SaguiOS era só cliente (o wget, ver http.c). Este
 * arquivo é o outro lado: aceitar uma conexão, falar um protocolo de
 * arquivos em linhas e fechar. O cliente é o sagui-fs.ps1 do Windows, que
 * chega pela NAT do VirtualBox com o encaminhamento de porta do host
 * (127.0.0.1:2500 -> convidado 2500) ou pela user-net do QEMU (hostfwd).
 *
 * O protocolo SAGUI/1 é texto em Latin-1, uma linha por vez, sempre \n:
 *
 *   servidor -> cliente:  "SAGUI/1"                (banner, ao conectar)
 *   LIST <pasta>      ->  "ENTRADAS <n>" + n linhas "<A|P|X> <tamanho> <nome>"
 *   INFO <caminho>    ->  "TIPO <A|P|X> <tamanho>"
 *   GET <caminho>     ->  "DADOS <n>" + n bytes crus
 *   PUT <caminho> <n> ->  "PRONTO"; cliente manda n bytes; servidor -> "OK"
 *   RM / MKDIR / RMDIR / REN -> "OK"
 *   SAIR              ->  "TCHAU" e a conexão se fecha
 *   qualquer problema ->  "ERRO <código> <motivo>" (código do abi.h, positivo)
 *
 * "A" é arquivo, "P" pasta e "X" programa. No PUT o tamanho é o último
 * campo da linha, então o caminho pode ter espaços; no REN a divisão é no
 * primeiro espaço (o destino aceita espaços, a origem não).
 *
 * O TCP aqui é do mesmo tamanho do do http.c: uma conexão por vez, sem
 * reordenação (segmento fora de ordem derruba a conexão), retransmissão
 * só do que não foi confirmado e janela de recepção fixa do tamanho da
 * fila. O que muda é o lado: aqui o SYN chega, e o ISN é novo a cada
 * conexão. Como a pilha é serializada (cadeado da rede, D040), cada fatia
 * da chamada de sistema atende uma conexão inteira.
 *
 * Os limites: linha de comando de 512 bytes, LIST de até 128 entradas e
 * PUT de até 8 MiB — o suficiente para trocar arquivos de desenvolvimento
 * sem estourar a memória do kernel. Caminhos no pendrive (/usb montado)
 * não passam: o servidor só enxerga a árvore em RAM.
 */
#include "servidor.h"
#include "rede.h"
#include "pacotes.h"
#include "sistema.h"
#include "processo.h"
#include "vfs.h"
#include "caminho.h"
#include "texto.h"
#include "console.h"
#include "relogio.h"
#include "abi.h"

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10
#define TCP_LEN 20

#define PEDACO_MAX    1400          /* dados por segmento nosso (MTU 1500) */
#define FILA_CAP      4096          /* fila de recepção = janela anunciada */
#define LINHA_MAX     512           /* maior linha de comando aceita */
#define LISTA_MAX     128           /* entradas devolvidas por um LIST */
#define PUT_MAX       (8u << 20)    /* 8 MiB é o maior PUT aceito */
#define ESPERA_ACK_MS 600           /* espera do ACK antes de retransmitir */
#define ACK_TENTATIVAS 3
#define INATIVO_MS    30000         /* cliente calado por isto: conexão cai */
#define TOTAL_MS      600000        /* prazo total de uma conexão */

/* --------------------------------------------------------- estado */
/* Uma conexão por vez (a pilha é serializada), então o estado todo é
 * estático, como no http.c. */
static uint32_t ip_cliente;
static uint16_t porta_cliente;
static uint32_t isn_local;        /* nosso número de sequência inicial */
static uint32_t nosso_seq;        /* próximo byte que vamos mandar */
static uint32_t enviado_fim;      /* até onde já mandamos (valida os ACKs) */
static uint32_t confirmado;       /* até onde o cliente confirmou */
static uint32_t esperado;         /* próximo byte que esperamos do cliente */
static bool     cliente_fechou;   /* o cliente mandou FIN: nada mais vem */

static uint8_t  seg[1500];        /* segmento de saída */
static uint8_t  rx[1520];         /* segmento de chegada */
static uint8_t  fila[FILA_CAP];   /* bytes recebidos ainda não processados */
static uint16_t fila_ini, fila_fim;
static uint8_t  pedaco[PEDACO_MAX];       /* pedaço de arquivo nos dois sentidos */
static entrada_pasta_t lista[LISTA_MAX];  /* entradas guardadas pelo LIST */

/* O último segmento recebido, preenchido por seg_receber(). */
static uint8_t     rx_flags;
static uint32_t    rx_seq, rx_ack;
static uint16_t    rx_tam;
static const uint8_t *rx_dados;

static char resp[LINHA_MAX + 64];       /* resposta em texto */

enum { CONX_NADA = 0, CONX_DADOS, CONX_FIM, CONX_ERRO };

/* ------------------------------------------------------------ utilidades */
static uint32_t fila_bytes(void) { return (uint32_t)(fila_fim - fila_ini); }
static uint32_t fila_livre(void) { return FILA_CAP - fila_bytes(); }

/* Tira "n" bytes do começo da fila (copia para "dst" se não for NULL) e
 * junta o resto no começo, para o espaço nunca fragmentar. */
static void fila_tirar(void *dst, uint16_t n)
{
    if (n > fila_fim - fila_ini) n = (uint16_t)(fila_fim - fila_ini);
    if (dst) memcpy(dst, fila + fila_ini, n);
    memmove(fila, fila + fila_ini + n, (size_t)(fila_fim - fila_ini - n));
    fila_fim = (uint16_t)(fila_fim - fila_ini - n);
    fila_ini = 0;
}

/* Número de sequência inicial: muda a cada conexão (repetir confundiria um
 * cliente que voltou depois de um erro). Mistura o relógio, um contador
 * que salta sem repetir cedo e o pid. */
static uint32_t gerar_isn(void)
{
    static uint32_t salto;
    salto += 0x9E3779B9u;
    uint32_t isn = relogio_tiques() ^ (salto >> 3) ^ ((uint32_t)atual->pid << 16);
    return (isn * 2654435761u) & 0x7FFFFFFFu;
}

/* --------------------------------------------------------------- TCP */
static uint16_t tcp_soma(uint32_t destino, const uint8_t *t, uint16_t n)
{
    uint8_t pseudo[12];
    pct_put32(pseudo, rede_info()->ip);
    pct_put32(pseudo + 4, destino);
    pseudo[8] = 0; pseudo[9] = 6;
    pct_put16(pseudo + 10, n);
    return pct_checksum(t, n, pct_soma(pseudo, 12, 0));
}

/* Manda um segmento para o cliente. A janela anunciada é o espaço real da
 * fila de recepção: o cliente para de mandar quando ela enche. */
static bool tcp_enviar(uint8_t flags, const uint8_t *dados, uint16_t n)
{
    if ((uint32_t)TCP_LEN + n > sizeof seg) return false;
    uint8_t *t = seg;
    pct_put16(t, SERVIDOR_PORTA);
    pct_put16(t + 2, porta_cliente);
    pct_put32(t + 4, nosso_seq);
    pct_put32(t + 8, (flags & TCP_ACK) ? esperado : 0);
    t[12] = 0x50;                  /* 5 palavras de cabeçalho, sem opções */
    t[13] = flags;
    pct_put16(t + 14, (uint16_t)fila_livre());
    pct_put16(t + 16, 0);
    pct_put16(t + 18, 0);
    for (uint16_t i = 0; i < n; i++) t[TCP_LEN + i] = dados[i];
    pct_put16(t + 16, tcp_soma(ip_cliente, t, (uint16_t)(TCP_LEN + n)));
    /* SYN e FIN consomem um número de sequência cada. */
    uint32_t consumo = n + ((flags & (TCP_SYN | TCP_FIN)) ? 1u : 0u);
    if (nosso_seq + consumo > enviado_fim) enviado_fim = nosso_seq + consumo;
    return pct_ip_enviar(ip_cliente, 6, t, (uint16_t)(TCP_LEN + n));
}

/* Espera até "ms" por um segmento desta conexão (dados, ACK puro, FIN,
 * RST: qualquer um). Devolve false no prazo. */
static bool seg_receber(uint32_t ms)
{
    pct_prazo_t prazo;
    pct_prazo_iniciar(&prazo, ms);
    while (!pct_prazo_acabou(&prazo)) {
        uint16_t n = 0;
        if (!pct_ip_receber(6, ip_cliente, rx, sizeof rx, &n, 20, NULL)) { pct_pausa(); continue; }
        if (n < TCP_LEN) continue;
        if (pct_get16(rx + 2) != SERVIDOR_PORTA) continue;   /* outra conexão */
        uint16_t cab = (uint16_t)((rx[12] >> 4) * 4);
        if (cab < TCP_LEN || cab > n) continue;
        rx_flags = rx[13];
        rx_seq = pct_get32(rx + 4);
        rx_ack = pct_get32(rx + 8);
        /* O ACK do cliente só vale entre o que já mandamos e o fim enviado. */
        if ((rx_flags & TCP_ACK) && rx_ack > confirmado && rx_ack <= enviado_fim)
            confirmado = rx_ack;
        rx_tam = (uint16_t)(n - cab);
        rx_dados = rx + cab;
        return true;
    }
    return false;
}

/* Processa o último segmento recebido: guarda a carga na fila, manda ACK,
 * detecta retransmissão duplicada e fechamento. */
static int conx_processar(void)
{
    if (rx_flags & TCP_RST) return CONX_ERRO;
    /* Retransmissão de algo que já processamos (nosso ACK se perdeu): só
     * confirma de novo e joga fora. */
    if (rx_seq < esperado && (rx_tam || (rx_flags & TCP_FIN))) {
        tcp_enviar(TCP_ACK, NULL, 0);
        return CONX_NADA;
    }
    if (rx_tam) {
        if (rx_seq != esperado) return CONX_ERRO;     /* fora de ordem: cai */
        if (rx_tam > fila_livre()) return CONX_ERRO;  /* a janela não deixaria */
        memcpy(fila + fila_fim, rx_dados, rx_tam);
        fila_fim = (uint16_t)(fila_fim + rx_tam);
        esperado += rx_tam;
        tcp_enviar(TCP_ACK, NULL, 0);
    }
    if (rx_flags & TCP_FIN) {
        esperado++;
        tcp_enviar(TCP_ACK, NULL, 0);
        cliente_fechou = true;
        return rx_tam ? CONX_DADOS : CONX_FIM;
    }
    return rx_tam ? CONX_DADOS : CONX_NADA;
}

/* Espera até "ms" processando o que chega. CONX_DADOS quando entraram
 * bytes; CONX_FIM/CONX_ERRO quando a conexão acabou; CONX_NADA no prazo. */
static int conx_esperar(uint32_t ms)
{
    pct_prazo_t prazo;
    pct_prazo_iniciar(&prazo, ms);
    while (!pct_prazo_acabou(&prazo)) {
        if (!seg_receber(20)) { pct_pausa(); continue; }
        int r = conx_processar();
        if (r != CONX_NADA) return r;
    }
    return CONX_NADA;
}

/* Manda "n" bytes para o cliente em pedaços de PEDACO_MAX, esperando o ACK
 * de cada um (com retransmissão). O que chegar nesse meio-tempo vai para a
 * fila, como sempre. */
static bool conx_enviar(const uint8_t *dados, uint32_t n)
{
    uint32_t off = 0;
    while (off < n) {
        uint16_t leva = (n - off > PEDACO_MAX) ? PEDACO_MAX : (uint16_t)(n - off);
        uint32_t fim = nosso_seq + leva;
        bool ok = false;
        for (int t = 0; t < ACK_TENTATIVAS && !ok; t++) {
            if (!tcp_enviar(TCP_ACK | TCP_PSH, dados + off, leva)) return false;
            pct_prazo_t prazo;
            pct_prazo_iniciar(&prazo, ESPERA_ACK_MS);
            while (!pct_prazo_acabou(&prazo)) {
                if (!seg_receber(20)) { pct_pausa(); continue; }
                if (conx_processar() == CONX_ERRO) return false;
                if (confirmado >= fim) { ok = true; break; }
            }
        }
        if (!ok) return false;
        nosso_seq = fim;
        off += leva;
    }
    return true;
}

/* --------------------------------------------------------- a conexão */
/* Espera até "ms" por um cliente ligando na porta do servidor. */
static bool aceitar(uint32_t ms)
{
    pct_prazo_t prazo;
    pct_prazo_iniciar(&prazo, ms);
    while (!pct_prazo_acabou(&prazo)) {
        if (atual->estado == P_ZUMBI) return false;   /* nos mataram: solta a pilha */
        uint16_t n = 0;
        uint32_t quem = 0;
        if (!pct_ip_receber(6, 0, rx, sizeof rx, &n, 20, &quem)) { pct_pausa(); continue; }
        if (n < TCP_LEN) continue;
        if (pct_get16(rx + 2) != SERVIDOR_PORTA) continue;
        if (!(rx[13] & TCP_SYN) || (rx[13] & TCP_ACK)) continue;
        ip_cliente = quem;
        porta_cliente = pct_get16(rx);
        esperado = pct_get32(rx + 4) + 1;             /* o SYN consome um número */
        klog("[SERVIDOR] conexao de %u.%u.%u.%u:%u\n",
             (ip_cliente >> 24) & 255, (ip_cliente >> 16) & 255,
             (ip_cliente >> 8) & 255, ip_cliente & 255, porta_cliente);
        return true;
    }
    return false;
}

/* Completa o aperto de mão de três vias e manda o banner. */
static bool aperto_de_mao(void)
{
    bool ok = false;
    for (int t = 0; t < ACK_TENTATIVAS && !ok; t++) {
        if (!tcp_enviar(TCP_SYN | TCP_ACK, NULL, 0)) return false;
        pct_prazo_t prazo;
        pct_prazo_iniciar(&prazo, ESPERA_ACK_MS);
        while (!pct_prazo_acabou(&prazo)) {
            if (!seg_receber(20)) { pct_pausa(); continue; }
            if (rx_flags & TCP_RST) return false;
            if ((rx_flags & TCP_SYN) && !(rx_flags & TCP_ACK)) {
                tcp_enviar(TCP_SYN | TCP_ACK, NULL, 0);   /* o cliente não viu */
                continue;
            }
            (void)conx_processar();      /* o ACK final pode vir com dados */
            if (confirmado >= isn_local + 1) { ok = true; break; }
        }
    }
    if (!ok) return false;
    nosso_seq = isn_local + 1;
    return conx_enviar((const uint8_t *)"SAGUI/1\n", 8);
}

/* Nosso FIN e a espera do ACK dele. */
static void conexao_fechar(void)
{
    uint32_t fin_seq = nosso_seq;
    bool ok = false;
    for (int t = 0; t < ACK_TENTATIVAS && !ok; t++) {
        if (!tcp_enviar(TCP_FIN | TCP_ACK, NULL, 0)) break;
        pct_prazo_t prazo;
        pct_prazo_iniciar(&prazo, ESPERA_ACK_MS);
        while (!pct_prazo_acabou(&prazo)) {
            if (!seg_receber(20)) { pct_pausa(); continue; }
            if (conx_processar() == CONX_ERRO) return;
            if (confirmado >= fin_seq + 1) { ok = true; break; }
        }
    }
    nosso_seq = fin_seq + 1;
}

/* Lê uma linha da fila, esperando o que falta chegar. 0 = ok; -1 = a
 * conexão acabou; -2 = cliente calado demais; -3 = linha longa demais. */
static int linha_ler(char *linha, uint32_t max)
{
    uint32_t fim = relogio_tiques() + INATIVO_MS / 10;
    for (;;) {
        for (uint16_t i = fila_ini; i < fila_fim; i++) {
            if (fila[i] != '\n') continue;
            uint16_t tam = (uint16_t)(i - fila_ini);
            if (tam >= max) return -3;
            memcpy(linha, fila + fila_ini, tam);
            linha[tam] = 0;
            if (tam && linha[tam - 1] == '\r') linha[tam - 1] = 0;   /* CRLF */
            fila_tirar(NULL, (uint16_t)(tam + 1));
            return 0;
        }
        if (fila_bytes() >= max) return -3;
        if (cliente_fechou) return -1;
        if ((int32_t)(relogio_tiques() - fim) >= 0) return -2;
        int r = conx_esperar(200);
        if (r == CONX_FIM || r == CONX_ERRO) return -1;
    }
}

/* ------------------------------------------------------------ comandos */
static bool mandar_texto(const char *s)
{
    return conx_enviar((const uint8_t *)s, (uint32_t)txt_tamanho(s));
}

static const char *texto_erro(int erro)
{
    switch (erro) {
    case ERRO_NAO_EXISTE:   return "nao existe";
    case ERRO_JA_EXISTE:    return "ja existe";
    case ERRO_E_PASTA:      return "e pasta";
    case ERRO_NAO_E_PASTA: return "nao e pasta";
    case ERRO_NAO_VAZIA:    return "pasta nao vazia";
    case ERRO_ACESSO:      return "nao permitido";
    case ERRO_SEM_MEMORIA: return "sem memoria";
    case ERRO_LIMITE:      return "passa do limite";
    default:                return "erro";
    }
}

static bool mandar_erro(int codigo, const char *motivo)
{
    snformatar(resp, sizeof resp, "ERRO %d %s\n", -codigo, motivo);
    return mandar_texto(resp);
}

/* O VFS não tem cadeado: as outras chamadas de sistema rodam com as
 * interrupções desligadas do começo ao fim, e o servidor é o único que
 * toca nele com elas ligadas (o polling precisa do relógio). Cada uso do
 * VFS aqui entra numa seção curta com as interrupções desligadas. */
static no_t *achar_segurando(const char *caminho)
{
    __asm__ volatile("cli" ::: "memory");
    no_t *x = vfs_achar(caminho);
    if (x) vfs_segurar(x);
    __asm__ volatile("sti" ::: "memory");
    return x;
}

/* Caminho do comando (relativo à pasta do processo que rodou o servidor)
 * -> caminho absoluto simplificado. */
static bool resolver(const char *alvo, char *saida)
{
    return caminho_resolver(atual->pasta, alvo, saida, CAMINHO_MAX);
}

/* Recusa educada do pendrive: a árvore em RAM é o que o servidor enxerga. */
static bool no_pendrive(const char *caminho)
{
    const char *rel;
    if (!vfs_na_usb(caminho, &rel)) return false;
    mandar_erro(ERRO_ACESSO, "o pendrive nao passa pelo servidor");
    return true;
}

static bool cmd_listar(const char *alvo)
{
    char caminho[CAMINHO_MAX];
    if (!resolver(alvo, caminho)) return mandar_erro(ERRO_INVALIDO, "caminho comprido demais");
    if (no_pendrive(caminho)) return true;
    __asm__ volatile("cli" ::: "memory");
    no_t *x = vfs_achar(caminho);
    if (!x) { __asm__ volatile("sti" ::: "memory"); return mandar_erro(ERRO_NAO_EXISTE, texto_erro(ERRO_NAO_EXISTE)); }
    if (x->tipo != TIPO_PASTA) { __asm__ volatile("sti" ::: "memory"); return mandar_erro(ERRO_NAO_E_PASTA, "nao e pasta"); }
    int total = 0;
    while (total < LISTA_MAX && vfs_listar(caminho, total, &lista[total]) == 1) total++;
    __asm__ volatile("sti" ::: "memory");

    snformatar(resp, sizeof resp, "ENTRADAS %d\n", total);
    if (!mandar_texto(resp)) return false;
    for (int i = 0; i < total; i++) {
        char tipo = lista[i].tipo == TIPO_PASTA ? 'P' :
                   lista[i].tipo == TIPO_PROGRAMA ? 'X' : 'A';
        snformatar(resp, sizeof resp, "%c %u %s\n", tipo, lista[i].tamanho, lista[i].nome);
        if (!mandar_texto(resp)) return false;
    }
    return true;
}

static bool cmd_info(const char *alvo)
{
    char caminho[CAMINHO_MAX];
    if (!resolver(alvo, caminho)) return mandar_erro(ERRO_INVALIDO, "caminho comprido demais");
    if (no_pendrive(caminho)) return true;
    no_t *x = achar_segurando(caminho);
    if (!x) return mandar_erro(ERRO_NAO_EXISTE, texto_erro(ERRO_NAO_EXISTE));
    char tipo = x->tipo == TIPO_PASTA ? 'P' : x->tipo == TIPO_PROGRAMA ? 'X' : 'A';
    uint32_t tamanho = x->tamanho;
    __asm__ volatile("cli" ::: "memory");
    vfs_soltar(x);
    __asm__ volatile("sti" ::: "memory");
    snformatar(resp, sizeof resp, "TIPO %c %u\n", tipo, tamanho);
    return mandar_texto(resp);
}

static bool cmd_get(const char *alvo)
{
    char caminho[CAMINHO_MAX];
    if (!resolver(alvo, caminho)) return mandar_erro(ERRO_INVALIDO, "caminho comprido demais");
    if (no_pendrive(caminho)) return true;
    no_t *x = achar_segurando(caminho);   /* segura: um rm no meio não libera */
    if (!x) return mandar_erro(ERRO_NAO_EXISTE, texto_erro(ERRO_NAO_EXISTE));
    if (x->tipo == TIPO_PASTA) {
        __asm__ volatile("cli" ::: "memory"); vfs_soltar(x); __asm__ volatile("sti" ::: "memory");
        return mandar_erro(ERRO_E_PASTA, "e pasta");
    }
    snformatar(resp, sizeof resp, "DADOS %u\n", x->tamanho);
    bool ok = mandar_texto(resp);
    uint32_t off = 0;
    while (ok && off < x->tamanho) {
        uint32_t leva = x->tamanho - off;
        if (leva > PEDACO_MAX) leva = PEDACO_MAX;
        __asm__ volatile("cli" ::: "memory");
        uint32_t lidos = vfs_ler(x, off, pedaco, leva);
        __asm__ volatile("sti" ::: "memory");
        if (!lidos) break;
        off += lidos;
        ok = conx_enviar(pedaco, lidos);
    }
    __asm__ volatile("cli" ::: "memory"); vfs_soltar(x); __asm__ volatile("sti" ::: "memory");
    return ok;
}

/* "PUT caminho tamanho": o tamanho é o último campo, então o caminho pode
 * ter espaços. Depois do "PRONTO" o cliente manda os bytes e recebe "OK". */
static bool cmd_put(const char *resto)
{
    uint32_t tam = (uint32_t)txt_tamanho(resto);
    int esp = -1;
    for (int i = (int)tam - 1; i >= 0; i--)
        if (resto[i] == ' ') { esp = i; break; }
    if (esp < 1) return mandar_erro(ERRO_INVALIDO, "uso: PUT <caminho> <bytes>");
    char bruto[CAMINHO_MAX];
    if ((uint32_t)esp >= sizeof bruto) return mandar_erro(ERRO_INVALIDO, "caminho comprido demais");
    memcpy(bruto, resto, (size_t)esp);
    bruto[esp] = 0;

    uint32_t n = 0;
    for (const char *q = resto + esp + 1; *q; q++) {
        if (*q < '0' || *q > '9') return mandar_erro(ERRO_INVALIDO, "tamanho invalido");
        if (n > PUT_MAX) break;               /* já estourou: para de somar */
        n = n * 10u + (uint32_t)(*q - '0');
    }
    if (n > PUT_MAX) return mandar_erro(ERRO_LIMITE, "maior que 8 MiB");

    char caminho[CAMINHO_MAX];
    if (!resolver(bruto, caminho)) return mandar_erro(ERRO_INVALIDO, "caminho comprido demais");
    if (no_pendrive(caminho)) return true;

    __asm__ volatile("cli" ::: "memory");
    no_t *x = vfs_achar(caminho);
    int r = 0;
    if (x && x->tipo == TIPO_PASTA) { __asm__ volatile("sti" ::: "memory"); return mandar_erro(ERRO_E_PASTA, "e pasta"); }
    if (x) vfs_truncar(x);
    else r = vfs_criar(caminho, TIPO_ARQUIVO, &x);
    if (r == 0) vfs_segurar(x);
    __asm__ volatile("sti" ::: "memory");
    if (r < 0) return mandar_erro(r, texto_erro(r));

    if (!mandar_texto("PRONTO\n")) {
        __asm__ volatile("cli" ::: "memory"); vfs_soltar(x); __asm__ volatile("sti" ::: "memory");
        return false;
    }

    uint32_t recebido = 0;
    uint32_t fim = relogio_tiques() + INATIVO_MS / 10;
    while (recebido < n) {
        if (fila_bytes()) {
            uint32_t leva = n - recebido;
            if (leva > fila_bytes()) leva = fila_bytes();
            if (leva > PEDACO_MAX) leva = PEDACO_MAX;
            fila_tirar(pedaco, (uint16_t)leva);
            __asm__ volatile("cli" ::: "memory");
            int w = vfs_escrever(x, recebido, pedaco, leva);
            __asm__ volatile("sti" ::: "memory");
            if (w < 0) break;                 /* cliente percebe pela conexão */
            recebido += leva;
            fim = relogio_tiques() + INATIVO_MS / 10;
        } else {
            if (cliente_fechou) break;
            if ((int32_t)(relogio_tiques() - fim) >= 0) break;
            int c = conx_esperar(200);
            if (c == CONX_FIM || c == CONX_ERRO) break;
        }
    }
    __asm__ volatile("cli" ::: "memory"); vfs_soltar(x); __asm__ volatile("sti" ::: "memory");
    return recebido == n ? mandar_texto("OK\n") : false;
}

/* RM, RMDIR: um caminho e uma função do VFS com esta cara. */
static bool cmd_vfs(const char *alvo, int (*funcao)(const char *))
{
    char caminho[CAMINHO_MAX];
    if (!resolver(alvo, caminho)) return mandar_erro(ERRO_INVALIDO, "caminho comprido demais");
    if (no_pendrive(caminho)) return true;
    __asm__ volatile("cli" ::: "memory");
    int r = funcao(caminho);
    __asm__ volatile("sti" ::: "memory");
    if (r < 0) return mandar_erro(r, texto_erro(r));
    return mandar_texto("OK\n");
}

static int criar_pasta_vfs(const char *caminho) { return vfs_criar(caminho, TIPO_PASTA, NULL); }

/* "REN origem destino": divide no primeiro espaço (a origem não pode ter
 * espaços; o destino pode). */
static bool cmd_renomear(const char *resto)
{
    const char *esp = resto;
    while (*esp && *esp != ' ') esp++;
    if (*esp != ' ' || esp == resto || !esp[1])
        return mandar_erro(ERRO_INVALIDO, "uso: REN <origem> <destino>");
    char origem[CAMINHO_MAX], destino[CAMINHO_MAX], bruto[CAMINHO_MAX];
    uint32_t n = (uint32_t)(esp - resto);
    if (n >= sizeof bruto) return mandar_erro(ERRO_INVALIDO, "caminho comprido demais");
    memcpy(bruto, resto, n);
    bruto[n] = 0;
    if (!resolver(bruto, origem)) return mandar_erro(ERRO_INVALIDO, "caminho comprido demais");
    if (!resolver(esp + 1, destino)) return mandar_erro(ERRO_INVALIDO, "caminho comprido demais");
    if (no_pendrive(origem) || no_pendrive(destino)) return true;
    __asm__ volatile("cli" ::: "memory");
    int r = vfs_renomear(origem, destino);
    __asm__ volatile("sti" ::: "memory");
    if (r < 0) return mandar_erro(r, texto_erro(r));
    return mandar_texto("OK\n");
}

/* Serve os comandos até o SAIR, um erro ou o prazo total. */
static bool comandos(void)
{
    char linha[LINHA_MAX];
    uint32_t fim_total = relogio_tiques() + TOTAL_MS / 10;
    for (;;) {
        if (atual->estado == P_ZUMBI) return false;
        if ((int32_t)(relogio_tiques() - fim_total) >= 0) return false;
        if (linha_ler(linha, sizeof linha) < 0) return false;

        const char *cmd, *resto;
        char *p = linha;
        while (*p && *p != ' ') p++;
        if (*p == ' ') { *p = 0; resto = p + 1; }
        else resto = p;
        cmd = linha;

        if (txt_compara(cmd, "SAIR") == 0) return mandar_texto("TCHAU\n");
        if (txt_compara(cmd, "LIST") == 0) { if (!cmd_listar(resto)) return false; }
        else if (txt_compara(cmd, "INFO") == 0) { if (!cmd_info(resto)) return false; }
        else if (txt_compara(cmd, "GET") == 0) { if (!cmd_get(resto)) return false; }
        else if (txt_compara(cmd, "PUT") == 0) { if (!cmd_put(resto)) return false; }
        else if (txt_compara(cmd, "RM") == 0) { if (!cmd_vfs(resto, vfs_apagar)) return false; }
        else if (txt_compara(cmd, "MKDIR") == 0) { if (!cmd_vfs(resto, criar_pasta_vfs)) return false; }
        else if (txt_compara(cmd, "RMDIR") == 0) { if (!cmd_vfs(resto, vfs_remover_pasta)) return false; }
        else if (txt_compara(cmd, "REN") == 0) { if (!cmd_renomear(resto)) return false; }
        else if (!mandar_erro(ERRO_INVALIDO, "comando desconhecido")) return false;
    }
}

/* Atende uma conexão inteira: aperto de mão, banner, comandos e fecho. */
static int32_t servir(void)
{
    cliente_fechou = false;
    fila_ini = fila_fim = 0;
    isn_local = gerar_isn();
    nosso_seq = isn_local;
    confirmado = 0;
    enviado_fim = isn_local;
    if (!aperto_de_mao()) return 1;     /* houve conexão, mesmo que curta */
    comandos();
    conexao_fechar();
    klog("[SERVIDOR] conexao encerrada\n");
    return 1;
}

/* ------------------------------------------------------- chamada de sistema */
int32_t servidor_arquivos(uint32_t ms)
{
    const rede_info_t *cfg = rede_info();
    if (!cfg->presente) return ERRO_ACESSO;
    if (!aceitar(ms)) return 0;        /* ninguém ligou nesta fatia */
    return servir();
}

/* SIS_SERVIDOR_ARQUIVOS (ms): o mesmo padrão do sis_http_obter — as IRQs
 * ligam para o relógio andar durante o polling, e o cadeado da pilha
 * evita que o wget entre no meio. */
int32_t sis_servidor_arquivos(uint32_t ms, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    if (ms > 30000) ms = 30000;
    if (!rede_pilha_reservar()) return ERRO_OCUPADO;
    __asm__ volatile("sti" ::: "memory");
    int32_t r = servidor_arquivos(ms);
    __asm__ volatile("cli" ::: "memory");
    rede_pilha_liberar();
    return r;
}
