/* http.c - HTTP/1.0 sem TLS por cima da placa de rede do sistema.
 *
 * A pilha é pequena de propósito: ARP, IPv4, UDP (só para o DNS) e um TCP
 * básico, o suficiente para o `wget` baixar uma página. O endereço, o gateway
 * e o DNS vêm de rede_info() (DHCP ou o padrão 10.0.2.x da NAT do VirtualBox
 * e do QEMU), então a mesma pilha funciona na NAT, em "Bridge" e em
 * "Host-only" do VirtualBox — nada aqui depende do MAC do gateway do slirp,
 * que era o que prendia a versão anterior ao QEMU.
 *
 * Não tem: HTTPS, fragmentação de IPv4, retransmissão com recuo, janela
 * deslizante, nem mais de uma conexão ao mesmo tempo.
 */
#include "http.h"
#include "rede.h"
#include "pacotes.h"
#include "sistema.h"

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

#define TCP_LEN 20
#define UDP_LEN 8
#define ESPERA_CONEXAO_MS 1200
#define ESPERA_DADOS_MS   8000
#define ESPERA_FINAL_MS   600

static uint16_t porta_local = 49152;
static uint32_t seq_local = 0x53414755u;
static uint32_t seq_remoto;
static uint8_t segmento[1500];
static uint8_t resposta[600];

/* --------------------------------------------------------------- DNS */
static bool dns_montar(const char *nome, uint16_t id, uint8_t *consulta, uint16_t *tamanho)
{
    uint16_t k = 12;
    pct_put16(consulta, id);
    pct_put16(consulta + 2, 0x0100);     /* consulta padrão, recursiva */
    pct_put16(consulta + 4, 1);
    pct_put16(consulta + 6, 0);
    pct_put16(consulta + 8, 0);
    pct_put16(consulta + 10, 0);
    const char *s = nome;
    while (*s) {
        const char *e = s;
        while (*e && *e != '.') e++;
        uint32_t tam = (uint32_t)(e - s);
        if (!tam || tam > 63 || k + tam + 6 >= 256) return false;
        consulta[k++] = (uint8_t)tam;
        while (s < e) consulta[k++] = (uint8_t)*s++;
        if (*s == '.') s++;
    }
    consulta[k++] = 0;
    pct_put16(consulta + k, 1); k = (uint16_t)(k + 2);   /* tipo A */
    pct_put16(consulta + k, 1); k = (uint16_t)(k + 2);   /* classe IN */
    *tamanho = k;
    return true;
}

/* Percorre os registros de resposta e devolve o primeiro IPv4. */
static bool dns_interpretar(const uint8_t *r, uint16_t n, uint16_t id, uint32_t *ip)
{
    if (n < 12) return false;
    uint16_t cab = pct_get16(r);
    if (cab != id) return false;
    if (pct_get16(r + 2) & 0x000F) return false;         /* erro do servidor */
    uint16_t qd = pct_get16(r + 4), an = pct_get16(r + 6);
    uint16_t off = 12;
    for (uint16_t i = 0; i < qd && off < n; i++) {
        while (off < n && r[off]) {
            if ((r[off] & 0xC0) == 0xC0) { off = (uint16_t)(off + 2); break; }
            off = (uint16_t)(off + 1 + r[off]);
        }
        if (off < n && r[off] == 0) off++;
        off = (uint16_t)(off + 4);
    }
    for (uint16_t i = 0; i < an && off + 10 <= n; i++) {
        if ((r[off] & 0xC0) == 0xC0) off = (uint16_t)(off + 2);
        else {
            while (off < n && r[off]) off = (uint16_t)(off + 1 + r[off]);
            off++;
        }
        if (off + 10 > n) return false;
        uint16_t tipo = pct_get16(r + off), classe = pct_get16(r + off + 2);
        uint16_t tam = pct_get16(r + off + 8);
        off = (uint16_t)(off + 10);
        if (off + tam > n) return false;
        if (tipo == 1 && classe == 1 && tam == 4) { *ip = pct_get32(r + off); return true; }
        off = (uint16_t)(off + tam);
    }
    return false;
}

static bool dns_resolver(const char *nome, uint32_t *ip)
{
    const rede_info_t *cfg = rede_info();
    if (!cfg->dns) return false;
    uint8_t consulta[256];
    uint16_t tamanho;
    uint16_t id = (uint16_t)(0x5347u + (porta_local & 0x0Fu));
    if (!dns_montar(nome, id, consulta, &tamanho)) return false;

    uint8_t *u = segmento;
    for (uint16_t tentativa = 0; tentativa < 3; tentativa++) {
        pct_put16(u, porta_local++);
        pct_put16(u + 2, 53);
        pct_put16(u + 4, (uint16_t)(UDP_LEN + tamanho));
        pct_put16(u + 6, 0);                 /* checksum do UDP é opcional */
        for (uint16_t i = 0; i < tamanho; i++) u[UDP_LEN + i] = consulta[i];
        if (!pct_ip_enviar(cfg->dns, 17, u, (uint16_t)(UDP_LEN + tamanho))) continue;

        uint16_t n = 0;
        if (!pct_ip_receber(17, cfg->dns, resposta, sizeof resposta, &n, 700, NULL)) continue;
        if (n < UDP_LEN + 12) continue;
        if (pct_get16(resposta) != 53) continue;      /* veio do servidor DNS */
        uint16_t tam_udp = pct_get16(resposta + 4);
        if (tam_udp < UDP_LEN || tam_udp > n) continue;
        if (dns_interpretar(resposta + UDP_LEN, (uint16_t)(tam_udp - UDP_LEN), id, ip)) return true;
    }
    return false;
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

static bool tcp_enviar(uint32_t destino, uint16_t porta_destino, uint16_t porta_origem,
                       uint8_t flags, const uint8_t *dados, uint16_t n)
{
    uint8_t *t = segmento;
    pct_put16(t, porta_origem);
    pct_put16(t + 2, porta_destino);
    pct_put32(t + 4, seq_local);
    pct_put32(t + 8, (flags & TCP_ACK) ? seq_remoto : 0);
    t[12] = 0x50;                 /* 5 palavras de cabeçalho, sem opções */
    t[13] = flags;
    pct_put16(t + 14, 4096);      /* janela */
    pct_put16(t + 16, 0);
    pct_put16(t + 18, 0);
    for (uint16_t i = 0; i < n; i++) t[TCP_LEN + i] = dados[i];
    pct_put16(t + 16, tcp_soma(destino, t, (uint16_t)(TCP_LEN + n)));
    return pct_ip_enviar(destino, 6, t, (uint16_t)(TCP_LEN + n));
}

/* Espera um segmento TCP da conexão. Devolve a carga útil em "dados". */
static bool tcp_receber(uint32_t peer, uint16_t porta_destino, uint8_t *dados,
                        uint16_t capacidade, uint16_t *bytes, uint8_t *flags, uint32_t ms)
{
    *bytes = 0;
    pct_prazo_t prazo;
    pct_prazo_iniciar(&prazo, ms);
    while (!pct_prazo_acabou(&prazo)) {
        uint16_t n = 0;
        if (!pct_ip_receber(6, peer, resposta, sizeof resposta, &n, 20, NULL)) { pct_pausa(); continue; }
        if (n < TCP_LEN) continue;
        const uint8_t *t = resposta;
        if (pct_get16(t + 2) != porta_destino) continue;       /* outra conexão */
        uint16_t cab = (uint16_t)((t[12] >> 4) * 4);
        if (cab < TCP_LEN || cab > n) continue;
        uint32_t seq = pct_get32(t + 4);
        *flags = t[13];
        if (*flags & TCP_RST) { seq_remoto = seq + 1; return true; }
        uint16_t tam = (uint16_t)(n - cab);
        if (*flags & TCP_SYN) seq_remoto = seq + 1;
        else if (tam) seq_remoto = seq + tam;
        if (tam > capacidade) tam = capacidade;
        for (uint16_t i = 0; i < tam; i++) dados[i] = t[cab + i];
        *bytes = tam;
        return true;
    }
    return false;
}

/* --------------------------------------------------------------- URL */
static bool ler_url(const char *url, char *host, uint32_t tam_host,
                    uint16_t *porta, const char **caminho)
{
    if (!url) return false;
    for (int i = 0; i < 7; i++)
        if (url[i] != "http://"[i]) return false;
    const char *h = url + 7;
    uint32_t i = 0;
    while (h[i] && h[i] != '/' && h[i] != ':' && i < tam_host - 1) { host[i] = h[i]; i++; }
    host[i] = 0;
    if (!i) return false;
    *porta = 80;
    if (h[i] == ':') {
        uint32_t p = 0;
        i++;
        while (h[i] >= '0' && h[i] <= '9') {
            p = p * 10u + (uint32_t)(h[i++] - '0');
            if (p > 65535u) return false;
        }
        if (!p) return false;
        *porta = (uint16_t)p;
    }
    *caminho = h + i;
    if (!**caminho) *caminho = "/";
    return (**caminho == '/');
}

static bool ip_numerico(const char *host, uint32_t *ip)
{
    uint32_t valor = 0, octeto = 0;
    int partes = 0;
    for (int i = 0; host[i]; i++) {
        char c = host[i];
        if (c >= '0' && c <= '9') {
            octeto = octeto * 10u + (uint32_t)(c - '0');
            if (octeto > 255) return false;
        } else if (c == '.' && partes < 3) {
            valor = (valor << 8) | octeto;
            octeto = 0;
            partes++;
        } else {
            return false;
        }
    }
    if (partes != 3) return false;
    *ip = (valor << 8) | octeto;
    return true;
}

/* --------------------------------------------------------------- HTTP */
int32_t http_obter(const char *url, void *destino, uint32_t capacidade)
{
    const rede_info_t *cfg = rede_info();
    if (!cfg->presente || !url || !destino || capacidade < 128) return -1;

    char host[128];
    uint16_t porta;
    const char *caminho;
    if (!ler_url(url, host, sizeof host, &porta, &caminho)) return -1;

    uint32_t ip = 0;
    if (!ip_numerico(host, &ip) && !dns_resolver(host, &ip)) return -20;

    uint16_t porta_origem = porta_local++;
    uint32_t inicio = seq_local;
    seq_remoto = 0;

    /* Handshake: SYN, SYN+ACK, ACK. Duas tentativas. */
    bool conectado = false;
    for (int tentativa = 0; tentativa < 2 && !conectado; tentativa++) {
        if (!tcp_enviar(ip, porta, porta_origem, TCP_SYN, NULL, 0)) return -22;
        uint8_t dados[64], flags = 0;
        uint16_t n = 0;
        pct_prazo_t prazo;
        pct_prazo_iniciar(&prazo, ESPERA_CONEXAO_MS);
        while (!pct_prazo_acabou(&prazo)) {
            if (!tcp_receber(ip, porta_origem, dados, sizeof dados, &n, &flags, 100)) continue;
            if (flags & TCP_RST) return -23;
            if ((flags & TCP_SYN) && (flags & TCP_ACK)) { conectado = true; break; }
        }
    }
    if (!conectado) return -23;
    seq_local = inicio + 1;
    if (!tcp_enviar(ip, porta, porta_origem, TCP_ACK, NULL, 0)) return -1;

    /* Pedido. */
    char req[512];
    int q = 0;
    const char *pre = "GET ";
    while (*pre) req[q++] = *pre++;
    while (*caminho && q < 380) req[q++] = *caminho++;
    pre = " HTTP/1.0\r\nHost: ";
    while (*pre && q < 440) req[q++] = *pre++;
    for (int i = 0; host[i] && q < 480; i++) req[q++] = host[i];
    pre = "\r\nConnection: close\r\nUser-Agent: SaguiOS-wget/0.2\r\n\r\n";
    while (*pre && q < 508) req[q++] = *pre++;
    if (!tcp_enviar(ip, porta, porta_origem, TCP_ACK | TCP_PSH, (const uint8_t *)req, (uint16_t)q))
        return -24;
    seq_local += (uint32_t)q;

    /* Resposta: uma sequência de segmentos até o servidor fechar. */
    uint8_t *saida = (uint8_t *)destino;
    uint32_t usado = 0;
    uint32_t espera = ESPERA_DADOS_MS;
    while (usado < capacidade) {
        uint8_t dados[1500], flags = 0;
        uint16_t n = 0;
        if (!tcp_receber(ip, porta_origem, dados, sizeof dados, &n, &flags, espera)) break;
        if (flags & TCP_RST) return usado ? (int32_t)usado : -25;
        if (n) {
            uint16_t leva = n;
            if (leva > capacidade - usado) leva = (uint16_t)(capacidade - usado);
            for (uint16_t i = 0; i < leva; i++) saida[usado + i] = dados[i];
            usado += leva;
            seq_local += n;
            espera = ESPERA_FINAL_MS;       /* depois do primeiro dado, espera pouco */
            tcp_enviar(ip, porta, porta_origem, TCP_ACK, NULL, 0);
        }
        if (flags & TCP_FIN) {
            seq_remoto++;
            tcp_enviar(ip, porta, porta_origem, TCP_ACK, NULL, 0);
            break;
        }
    }
    return usado ? (int32_t)usado : -26;
}

int32_t sis_http_obter(uint32_t urlp, uint32_t dst, uint32_t cap, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    if (cap > 65536u || cap < 128u || !usuario_ok((void *)dst, cap, true)) return ERRO_ENDERECO;
    char url[256];
    int n = usuario_texto((const char *)urlp, url, sizeof url);
    if (n < 1) return n < 0 ? n : ERRO_INVALIDO;
    /* O servidor de arquivos (H48) passa fatias longas segurando a pilha;
     * sem o cadeado os buffers estáticos seriam pisados pelos dois lados. */
    if (!rede_pilha_reservar()) return ERRO_OCUPADO;
    /* int 0x80 usa interrupt gate e entra com IF=0. Habilitamos IRQs para
     * que o relógio ande durante o polling; a pilha é serializada por isso. */
    __asm__ volatile("sti" ::: "memory");
    int32_t r = http_obter(url, (void *)dst, cap);
    __asm__ volatile("cli" ::: "memory");
    rede_pilha_liberar();
    return r;
}
