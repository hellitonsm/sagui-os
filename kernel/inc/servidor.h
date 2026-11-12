/* servidor.h - servidor de arquivos pela rede (H48).
 *
 * Um TCP mínimo na porta 2500 aceita uma conexão de cada vez e fala o
 * protocolo SAGUI/1 (linhas Latin-1 terminadas em \n; ver servidor.c). É o
 * lado do sistema que atende o sagui-fs.ps1 do Windows, que chega pela
 * rede NAT do VirtualBox (com o encaminhamento de porta do host) ou pela
 * user-net do QEMU (hostfwd). */
#ifndef SAGUI_SERVIDOR_H
#define SAGUI_SERVIDOR_H
#include "tipos.h"

#define SERVIDOR_PORTA 2500

/* Espera até "ms" por um cliente e atende a conexão inteira. Devolve:
 *   1  -> uma conexão foi atendida (e já acabou);
 *   0  -> ninguém ligou dentro do prazo;
 *  <0  -> erro (ERRO_OCUPADO se outro processo está com a pilha de rede). */
int32_t servidor_arquivos(uint32_t ms);

#endif
