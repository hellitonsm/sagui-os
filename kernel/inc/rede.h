/* rede.h - interface de quadros Ethernet para a pilha de rede. */
#ifndef SAGUI_REDE_H
#define SAGUI_REDE_H
#include "tipos.h"

/* Inicializa o adaptador Intel 82540EM/8254x (QEMU -device e1000). */
bool e1000_iniciar(void);
bool e1000_disponivel(void);
const uint8_t *e1000_mac(void);
/* Envia/recebe quadros Ethernet completos, incluindo cabeçalho Ethernet.
 * Receber: 0 se não há quadro; senão comprimento (máx. capacidade passada). */
bool e1000_enviar(const void *quadro, uint16_t bytes);
uint16_t e1000_receber(void *quadro, uint16_t capacidade);
#endif
