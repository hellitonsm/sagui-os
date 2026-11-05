/* pacotes.h - utilitários de Ethernet/IPv4 compartilhados pela pilha de rede.
 *
 * O kernel não tem libc: aqui ficam as funções de montar/ler campos, as somas
 * de verificação e os dois atalhos que todo protocolo usa (mandar um IPv4 e
 * esperar um IPv4). O TCP do HTTP e o UDP do DHCP são montados por cima. */
#ifndef SAGUI_PACOTES_H
#define SAGUI_PACOTES_H
#include "tipos.h"

#define PCT_ETH_IP   0x0800
#define PCT_ETH_ARP  0x0806
#define PCT_ETH_LEN  14
#define PCT_IP_LEN   20
#define PCT_QUADRO_MAX 1514

uint32_t pct_soma(const uint8_t *p, uint32_t n, uint32_t s);
uint16_t pct_checksum(const uint8_t *p, uint32_t n, uint32_t s);
void     pct_put16(uint8_t *p, uint16_t v);
void     pct_put32(uint8_t *p, uint32_t v);
uint16_t pct_get16(const uint8_t *p);
uint32_t pct_get32(const uint8_t *p);

/* Ethernet puro (o destino é um MAC). */
bool pct_ethernet_enviar(uint16_t tipo, const uint8_t *destino_mac,
                         const uint8_t *carga, uint16_t bytes);

/* IPv4 com o cabeçalho montado aqui; o MAC do próximo salto vem do ARP. */
bool pct_ip_enviar(uint32_t destino, uint8_t protocolo, const uint8_t *carga, uint16_t bytes);

/* Espera até "ms" por um IPv4 do protocolo dado (0 = qualquer) vindo do IP
 * de origem "origem" (0 = qualquer). Copia só a carga (o que vem depois do
 * cabeçalho IP) e devolve o tamanho dela. */
bool pct_ip_receber(uint8_t protocolo, uint32_t origem, uint8_t *saida,
                    uint16_t capacidade, uint16_t *bytes, uint32_t ms);

/* Prazo: conta pelo relógio do sistema e, para o caso de o relógio não estar
 * andando (IRQs desligadas), também um número máximo de voltas. */
typedef struct { uint32_t fim; uint32_t voltas; } pct_prazo_t;

void pct_prazo_iniciar(pct_prazo_t *p, uint32_t ms);
bool pct_prazo_acabou(pct_prazo_t *p);
void pct_pausa(void);

#endif
