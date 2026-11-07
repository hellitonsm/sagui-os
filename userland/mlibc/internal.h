/* Internalos da camada POSIX do SaguiOS. Nao fazem parte da API publica. */
#ifndef SAGUI_MLIBC_INTERNAL_H
#define SAGUI_MLIBC_INTERNAL_H

#include "sagui.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>

#define MLIBC_DESCRITORES 16

/* Converte um resultado de syscall do kernel (0 ou positivo, erro negativo)
 * para a convencao POSIX (resultado ou -1 com errno definido). */
int __mlibc_resultado(int resultado);
int __mlibc_resultado_io(int resultado);
void __mlibc_erro(int codigo);

/* A tabela e so uma cache dos descritores usados pela API POSIX. O kernel
 * continua sendo a autoridade para validar cada operacao. */
void __mlibc_fd_definir(int fd, int flags);
void __mlibc_fd_esquecer(int fd);
int  __mlibc_fd_conhecido(int fd);
int  __mlibc_fd_flags(int fd);

/* Divisao sem sinal de 64 bits por divisor de 32 bits. Evita dependencia de
 * libgcc (__udivdi3) no executavel freestanding i686. */
unsigned long long __mlibc_divmod_u64(unsigned long long valor,
                                      unsigned divisor,
                                      unsigned *resto);

#endif
