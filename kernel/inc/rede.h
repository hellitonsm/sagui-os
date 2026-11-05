/* rede.h - interface das placas Ethernet e da configuração IP do SaguiOS.
 *
 * A pilha de rede fala com uma única placa, escolhida no boot pela ordem da
 * tabela de drivers (ver rede.c). Os drivers vivem em kernel/dispositivos/:
 *
 *   e1000 - Intel 82540EM/82545EM/82543GC. É a placa "Intel PRO/1000 MT
 *           Desktop" do VirtualBox e a -device e1000 do QEMU.
 *   pcnet - AMD Am79C970A/Am79C973 (PCnet-PCI/PCnet-FAST III). É a placa
 *           padrão do VirtualBox para máquinas de tipo "Outro/Desconhecido"
 *           e a -device pcnet do QEMU.
 *
 * O endereço IP, a máscara, o gateway e o DNS vêm de DHCP; se o servidor não
 * responder, a rede assume 10.0.2.15/24 com gateway 10.0.2.2 e DNS 10.0.2.3,
 * que é o padrão da rede NAT do VirtualBox e também a do QEMU user-net.
 */
#ifndef SAGUI_REDE_H
#define SAGUI_REDE_H
#include "tipos.h"

#define REDE_NOME_MAX 16

typedef struct {
    bool     presente;              /* achou uma placa conhecida            */
    char     driver[REDE_NOME_MAX]; /* "e1000", "pcnet"                     */
    uint16_t id_fabricante, id_modelo; /* identificação PCI da placa        */
    uint8_t  mac[6];
    bool     link;                  /* o cabo/rede parece ligado            */
    uint32_t ip, mascara, gateway, dns;
    bool     dhcp;                  /* endereço negociado por DHCP          */
    uint32_t dhcp_servidor;
    uint32_t enviados, recebidos, descartados;
} rede_info_t;

/* Liga a placa, negocia o endereço (DHCP com reserva estática) e deixa a
 * pilha pronta. Devolve false se nenhuma placa conhecida foi encontrada. */
bool rede_iniciar(void);
bool rede_disponivel(void);
const rede_info_t *rede_info(void);
const uint8_t *rede_mac(void);

/* Quadros Ethernet completos, com o cabeçalho de 14 bytes. Receber devolve 0
 * quando não há quadro pronto; o resto é polling (sem IRQ). */
bool     rede_enviar(const void *quadro, uint16_t bytes);
uint16_t rede_receber(void *quadro, uint16_t capacidade);

/* Descobre o MAC de quem responde por um IP (ARP, com cache de 8 entradas).
 * "proximo_salto" devolve o MAC do destino se ele está na mesma sub-rede, ou
 * o do gateway caso contrário — é o que a pilha IP precisa. */
bool rede_resolver(uint32_t ip, uint8_t *mac);
bool rede_proximo_salto(uint32_t destino, uint8_t *mac);

/* Os dois drivers (kernel/dispositivos/). */ 
bool e1000_iniciar(void);
bool e1000_enviar(const void *quadro, uint16_t bytes);
uint16_t e1000_receber(void *quadro, uint16_t capacidade);
const uint8_t *e1000_mac(void);
bool e1000_link(void);
uint16_t e1000_ids(uint16_t *fabricante, uint16_t *modelo);

bool pcnet_iniciar(void);
bool pcnet_enviar(const void *quadro, uint16_t bytes);
uint16_t pcnet_receber(void *quadro, uint16_t capacidade);
const uint8_t *pcnet_mac(void);
bool pcnet_link(void);
uint16_t pcnet_ids(uint16_t *fabricante, uint16_t *modelo);

#endif
