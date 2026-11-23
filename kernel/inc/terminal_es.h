/* terminal_es.h - o terminal como arquivo (entrada 0 e saída 1/2 dos processos). */
#ifndef SAGUI_TERMINAL_ES_H
#define SAGUI_TERMINAL_ES_H
#include "tipos.h"

int32_t terminal_ler(char *destino, uint32_t n);          /* pode devolver SIS_REPETIR */
int32_t terminal_escrever(const char *origem, uint32_t n);
void    terminal_modo(int modo);
void    terminal_limpar(void);

#endif
