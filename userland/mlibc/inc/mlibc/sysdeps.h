/* sysdeps.h - adapters locais usados para validar o contrato de sysdeps.
 *
 * Estes simbolos nao sao ABI do mlibc oficial. Retornam valores POSIX (0 ou
 * positivo; erro -1 com errno) e permitem testar os mapeamentos do SaguiOS. */
#ifndef MLIBC_SYSDEPS_H
#define MLIBC_SYSDEPS_H
#include "sagui.h"

void mlibc_exit(int codigo) __attribute__((noreturn));
int  mlibc_open(const char *caminho, int modo); /* modo ABRIR_* do SaguiOS */
int  mlibc_read(int fd, void *buf, unsigned n);
int  mlibc_write(int fd, const void *buf, unsigned n);
int  mlibc_seek(int fd, int desloc, int origem);
int  mlibc_close(int fd);
int  mlibc_clock_get(unsigned long *ms);
void mlibc_libc_log(const char *texto);
void mlibc_libc_panic(const char *texto) __attribute__((noreturn));
void *mlibc_anon_allocate(unsigned n);
int  mlibc_anon_free(void *p, unsigned n);

/* Futex/VM indisponiveis sem threads e mmap; falham com errno=ENOSYS. */
int  mlibc_futex_wait(void *uaddr, int val, int ms);
int  mlibc_futex_wake(void *uaddr, int n);
int  mlibc_vm_map(void *addr, unsigned n, void **saida);
int  mlibc_vm_unmap(void *addr, unsigned n);
int  mlibc_tcb_set(void *tcb);
#endif
