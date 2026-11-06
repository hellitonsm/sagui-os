/* sysdeps.h - sysdeps obrigatorios do mlibc adaptados ao SaguiOS.
 * Guia: docs.managarm.org/mlibc-book/porting/implementing_sysdeps_p2.html
 * No SaguiOS sem threads/TCB, Futex/VmMap/TcbSet sao stubs no-op;
 * o resto mapeia para int 0x80 via libsagui. */
#ifndef MLIBC_SYSDEPS_H
#define MLIBC_SYSDEPS_H
#include "sagui.h"
/* obrigatorios de verdade */
void mlibc_exit(int codigo);
int  mlibc_open(const char *caminho, int modo);
int  mlibc_read(int fd, void *buf, unsigned n);
int  mlibc_write(int fd, const void *buf, unsigned n);
int  mlibc_seek(int fd, int desloc, int origem);
int  mlibc_close(int fd);
int  mlibc_clock_get(unsigned long *ms);
void mlibc_libc_log(const char *texto);
void mlibc_libc_panic(const char *texto);
void *mlibc_anon_allocate(unsigned n);
int  mlibc_anon_free(void *p, unsigned n);
/* stubs sem threads/VM no SaguiOS */
int  mlibc_futex_wait(void *uaddr, int val, int ms);
int  mlibc_futex_wake(void *uaddr, int n);
int  mlibc_vm_map(void *addr, unsigned n, void **saida);
int  mlibc_vm_unmap(void *addr, unsigned n);
int  mlibc_tcb_set(void *tcb);
#endif
