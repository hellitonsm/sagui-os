#ifndef SAGUI_HTTP_H
#define SAGUI_HTTP_H
#include "tipos.h"
/* HTTP/1.0 simples sobre a rede NAT do QEMU; retorna bytes de resposta bruta. */
int32_t http_obter(const char *url, void *destino, uint32_t capacidade);
#endif
