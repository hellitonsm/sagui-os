/* dhcp.h - cliente DHCP mínimo (só o necessário para pegar endereço). */
#ifndef SAGUI_DHCP_H
#define SAGUI_DHCP_H
#include "tipos.h"

/* Pede um endereço ao servidor DHCP da rede. Devolve true e preenche os
 * quatro valores quando o servidor confirma (DHCPACK). "servidor" é o IP de
 * quem respondeu (útil para renovar depois). */
bool dhcp_negociar(uint32_t *ip, uint32_t *mascara, uint32_t *gateway,
                   uint32_t *dns, uint32_t *servidor);
#endif
