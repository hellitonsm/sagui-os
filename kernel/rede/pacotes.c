/* pacotes.c - utilitários de Ethernet/IPv4 (ver pacotes.h).
 *
 * O kernel usa o modelo de DMA do PC: os buffers são estáticos, na memória
 * física abaixo de 4 GiB, e o driver copia para dentro/fora deles. Aqui só
 * existe um quadro de trabalho de 1514 bytes, porque a pilha é serializada
 * (a chamada de sistema de HTTP desabilita interrupções enquanto roda).
 */
#include "pacotes.h"
#include "rede.h"
#include "relogio.h"

#define MAX_VOLTAS 20000000u   /* ~10 s de "pause" se o relógio não andar */

static uint8_t quadro[PCT_QUADRO_MAX];
static uint16_t ipid = 0x4251;

uint32_t pct_soma(const uint8_t *p, uint32_t n, uint32_t s)
{
    while (n > 1) { s += ((uint16_t)p[0] << 8) | p[1]; p += 2; n -= 2; }
    if (n) s += (uint16_t)p[0] << 8;
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return s;
}

uint16_t pct_checksum(const uint8_t *p, uint32_t n, uint32_t s)
{
    return (uint16_t)~pct_soma(p, n, s);
}

void pct_put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
void pct_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}
uint16_t pct_get16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
uint32_t pct_get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

void pct_pausa(void) { __asm__ volatile("pause"); }

void pct_prazo_iniciar(pct_prazo_t *p, uint32_t ms)
{
    p->fim = relogio_tiques() + (ms / 10u) + 1u;
    p->voltas = 0;
}

bool pct_prazo_acabou(pct_prazo_t *p)
{
    p->voltas++;
    if (p->voltas > MAX_VOLTAS) return true;
    return (int32_t)(relogio_tiques() - p->fim) >= 0;
}

bool pct_ethernet_enviar(uint16_t tipo, const uint8_t *destino_mac,
                         const uint8_t *carga, uint16_t bytes)
{
    if (!destino_mac || bytes > PCT_QUADRO_MAX - PCT_ETH_LEN) return false;
    const uint8_t *m = rede_mac();
    for (int i = 0; i < 6; i++) { quadro[i] = destino_mac[i]; quadro[6 + i] = m[i]; }
    pct_put16(quadro + 12, tipo);
    for (uint16_t i = 0; i < bytes; i++) quadro[PCT_ETH_LEN + i] = carga[i];
    uint16_t total = (uint16_t)(PCT_ETH_LEN + bytes);
    /* Ethernet exige 60 bytes de quadro; o resto é preenchido com zero. */
    for (uint16_t i = total; i < 60; i++) quadro[i] = 0;
    if (total < 60) total = 60;
    return rede_enviar(quadro, total);
}

bool pct_ip_enviar(uint32_t destino, uint8_t protocolo, const uint8_t *carga, uint16_t bytes)
{
    const rede_info_t *cfg = rede_info();
    if (!cfg->presente || bytes > PCT_QUADRO_MAX - PCT_ETH_LEN - PCT_IP_LEN) return false;
    uint8_t *p = quadro + PCT_ETH_LEN;
    p[0] = 0x45; p[1] = 0;
    pct_put16(p + 2, (uint16_t)(PCT_IP_LEN + bytes));
    pct_put16(p + 4, ipid++);
    pct_put16(p + 6, 0x4000);          /* não fragmentar */
    p[8] = 64; p[9] = protocolo;
    pct_put16(p + 10, 0);
    pct_put32(p + 12, cfg->ip);
    pct_put32(p + 16, destino);
    pct_put16(p + 10, pct_checksum(p, PCT_IP_LEN, 0));
    for (uint16_t i = 0; i < bytes; i++) p[PCT_IP_LEN + i] = carga[i];

    uint8_t mac[6];
    if (!rede_proximo_salto(destino, mac)) return false;
    const uint8_t *m = rede_mac();
    for (int i = 0; i < 6; i++) { quadro[i] = mac[i]; quadro[6 + i] = m[i]; }
    pct_put16(quadro + 12, PCT_ETH_IP);
    uint16_t total = (uint16_t)(PCT_ETH_LEN + PCT_IP_LEN + bytes);
    if (total < 60) { for (uint16_t i = total; i < 60; i++) quadro[i] = 0; total = 60; }
    return rede_enviar(quadro, total);
}

bool pct_ip_receber(uint8_t protocolo, uint32_t origem, uint8_t *saida,
                    uint16_t capacidade, uint16_t *bytes, uint32_t ms,
                    uint32_t *quem)
{
    const rede_info_t *cfg = rede_info();
    pct_prazo_t prazo;
    pct_prazo_iniciar(&prazo, ms);
    while (!pct_prazo_acabou(&prazo)) {
        uint16_t n = rede_receber(quadro, sizeof quadro);
        if (n < PCT_ETH_LEN + PCT_IP_LEN) { pct_pausa(); continue; }
        if (pct_get16(quadro + 12) != PCT_ETH_IP) continue;
        const uint8_t *ip = quadro + PCT_ETH_LEN;
        if ((ip[0] >> 4) != 4) continue;
        uint16_t cab = (uint16_t)((ip[0] & 0x0F) * 4);
        uint16_t total = pct_get16(ip + 2);
        if (total < cab || (uint32_t)total + PCT_ETH_LEN > n) continue;
        if (protocolo && ip[9] != protocolo) continue;
        if (origem && pct_get32(ip + 12) != origem) continue;
        uint32_t dst = pct_get32(ip + 16);
        /* Aceita o que é para nós, broadcast/limite e, antes do DHCP, tudo
         * (o nosso IP ainda é 0.0.0.0). */
        if (cfg->ip && dst != cfg->ip && dst != 0xFFFFFFFFu) continue;
        uint16_t carga = (uint16_t)(total - cab);
        if (carga > capacidade) carga = capacidade;
        for (uint16_t i = 0; i < carga; i++) saida[i] = ip[cab + i];
        *bytes = carga;
        if (quem) *quem = pct_get32(ip + 12);
        return true;
    }
    *bytes = 0;
    return false;
}
