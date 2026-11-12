/* dhcp.c - cliente DHCP (descobrir a rede sozinho).
 *
 * É o que faz o SaguiOS funcionar tanto na rede NAT do VirtualBox/QEMU quanto
 * nas redes "Bridge" e "Host-only" do VirtualBox, onde o endereço não é
 * 10.0.2.15. O roteiro é o clássico DORA:
 *
 *   DISCOVER (broadcast) -> OFFER -> REQUEST (broadcast) -> ACK
 *
 * Só existem duas mensagens de ida e uma de volta; nada de renovação, de
 * múltiplas ofertas ou de opções exóticas. Tudo em UDP sobre Ethernet, porque
 * o TCP do kernel (ver http.c) é para outra coisa.
 */
#include "dhcp.h"
#include "rede.h"
#include "pacotes.h"
#include "texto.h"

#define DHCP_SERVIDOR 67
#define DHCP_CLIENTE  68
#define DHCP_DESCUBRA 1
#define DHCP_OFERTA   2
#define DHCP_PEDIDO   3
#define DHCP_ACK      5
#define BOOTP_MIN     300      /* o pacote DHCP não pode ser menor que isto */
#define TENTATIVAS    3
#define ESPERA_MS     600

static uint8_t pacote[600];    /* cabeçalho UDP + BOOTP/DHCP */
static uint8_t resposta[600];

static uint32_t xid = 0x5A6011B7u;

/* ------------------------------------------------ opções da resposta */
typedef struct {
    uint32_t ip, mascara, gateway, dns, servidor;
    uint8_t  tipo;
    bool     tem_ip, tem_mascara, tem_gateway, tem_dns, tem_servidor;
} oferta_t;

static void ler_opcoes(const uint8_t *b, uint16_t limite, oferta_t *o)
{
    uint16_t i = 240;
    while (i < limite) {
        uint8_t codigo = b[i];
        if (codigo == 0) { i++; continue; }        /* preenchimento */
        if (codigo == 255) break;                  /* fim */
        if (i + 1 >= limite) break;
        uint8_t tam = b[i + 1];
        const uint8_t *v = b + i + 2;
        if (i + 2 + tam > limite) break;
        if (codigo == 53 && tam >= 1) o->tipo = v[0];
        else if (codigo == 1 && tam >= 4) { o->mascara = pct_get32(v); o->tem_mascara = true; }
        else if (codigo == 3 && tam >= 4) { o->gateway = pct_get32(v); o->tem_gateway = true; }
        else if (codigo == 6 && tam >= 4) { o->dns = pct_get32(v); o->tem_dns = true; }
        else if (codigo == 54 && tam >= 4) { o->servidor = pct_get32(v); o->tem_servidor = true; }
        i = (uint16_t)(i + 2 + tam);
    }
}

/* ------------------------------------------------ montagem do pedido */
static uint16_t montar(uint8_t tipo, uint32_t pedido_ip, uint32_t servidor_ip)
{
    uint8_t *b = pacote + 8;                       /* depois do cabeçalho UDP */
    memset(b, 0, BOOTP_MIN);
    b[0] = 1;             /* pedido (cliente -> servidor) */
    b[1] = 1;             /* Ethernet */
    b[2] = 6;             /* tamanho do endereço de hardware */
    pct_put32(b + 4, xid);
    pct_put16(b + 8, 0);  /* segundos desde o começo */
    pct_put16(b + 10, 0x8000);                     /* "responda em broadcast" */
    const uint8_t *mac = rede_mac();
    for (int i = 0; i < 6; i++) b[28 + i] = mac[i]; /* chaddr */
    pct_put32(b + 236, 0x63825363u);               /* "magic cookie" */

    uint16_t k = 240;
    b[k++] = 53; b[k++] = 1; b[k++] = tipo;        /* tipo da mensagem */
    b[k++] = 12; b[k++] = 5;                       /* nome do computador */
    b[k++] = 's'; b[k++] = 'a'; b[k++] = 'g'; b[k++] = 'u'; b[k++] = 'i';
    b[k++] = 55; b[k++] = 4;                       /* o que queremos receber */
    b[k++] = 1; b[k++] = 3; b[k++] = 6; b[k++] = 15;
    if (pedido_ip) { b[k++] = 50; b[k++] = 4; pct_put32(b + k, pedido_ip); k = (uint16_t)(k + 4); }
    if (servidor_ip) { b[k++] = 54; b[k++] = 4; pct_put32(b + k, servidor_ip); k = (uint16_t)(k + 4); }
    b[k++] = 255;
    if (k < BOOTP_MIN) k = BOOTP_MIN;              /* preenche até o mínimo */

    pct_put16(pacote, DHCP_CLIENTE);               /* porta de origem */
    pct_put16(pacote + 2, DHCP_SERVIDOR);          /* porta de destino */
    pct_put16(pacote + 4, (uint16_t)(8 + k));      /* tamanho do UDP */
    pct_put16(pacote + 6, 0);                      /* checksum do UDP é opcional */
    return (uint16_t)(8 + k);
}

/* O pedido vai em broadcast: antes do ACK o nosso IP ainda é 0.0.0.0. */
static bool enviar(uint8_t tipo, uint32_t pedido_ip, uint32_t servidor_ip)
{
    uint16_t n = montar(tipo, pedido_ip, servidor_ip);
    return pct_ip_enviar(0xFFFFFFFFu, 17, pacote, n);
}

/* Espera uma mensagem do servidor com o tipo desejado. */
static bool esperar(uint8_t tipo, oferta_t *o)
{
    pct_prazo_t prazo;
    pct_prazo_iniciar(&prazo, ESPERA_MS);
    while (!pct_prazo_acabou(&prazo)) {
        uint16_t n = 0;
        if (!pct_ip_receber(17, 0, resposta, sizeof resposta, &n, 20, NULL)) { pct_pausa(); continue; }
        if (n < 244) continue;
        if (pct_get16(resposta) != DHCP_SERVIDOR || pct_get16(resposta + 2) != DHCP_CLIENTE) continue;
        uint16_t tam_udp = pct_get16(resposta + 4);
        if (tam_udp < 8 || tam_udp > n) continue;
        const uint8_t *b = resposta + 8;
        if (b[0] != 2) continue;                            /* resposta do servidor */
        if (pct_get32(b + 4) != xid) continue;              /* outra conversa */
        memset(o, 0, sizeof *o);
        o->ip = pct_get32(b + 16);                          /* yiaddr */
        o->tem_ip = o->ip != 0;
        ler_opcoes(b, (uint16_t)(tam_udp - 8), o);
        if (o->tipo == tipo) return true;
    }
    return false;
}

bool dhcp_negociar(uint32_t *ip, uint32_t *mascara, uint32_t *gateway,
                   uint32_t *dns, uint32_t *servidor)
{
    if (!rede_disponivel()) return false;
    for (int tentativa = 0; tentativa < TENTATIVAS; tentativa++) {
        oferta_t oferta, ack;
        if (!enviar(DHCP_DESCUBRA, 0, 0)) continue;
        if (!esperar(DHCP_OFERTA, &oferta)) continue;
        if (!oferta.tem_ip) continue;
        uint32_t quem = oferta.tem_servidor ? oferta.servidor : 0;
        if (!enviar(DHCP_PEDIDO, oferta.ip, quem)) continue;
        if (!esperar(DHCP_ACK, &ack)) continue;
        if (!ack.tem_ip) { ack.ip = oferta.ip; }

        *ip = ack.ip;
        *mascara = ack.tem_mascara ? ack.mascara : (oferta.tem_mascara ? oferta.mascara : 0xFFFFFF00u);
        *gateway = ack.tem_gateway ? ack.gateway : oferta.gateway;
        *dns = ack.tem_dns ? ack.dns : oferta.dns;
        *servidor = ack.tem_servidor ? ack.servidor : quem;
        return true;
    }
    return false;
}
