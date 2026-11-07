/* sysdeps.c - callbacks do recorte mlibc/SaguiOS usados pela camada.
 *
 * Sao adapters locais, nao os simbolos C++/ABI do mlibc upstream. Futex e VM
 * nao existem no kernel; threads e mapeamentos arbitrarios ficam fora deste
 * subset. */
#include "internal.h"
#include <stdlib.h>
#include <unistd.h>
#include <mlibc/sysdeps.h>

void mlibc_exit(int codigo) { sair(codigo); }

int mlibc_open(const char *caminho, int modo)
{
    int fd = __mlibc_resultado(abrir(caminho, modo));
    if (fd >= 0) {
        int flags = 0;
        if ((modo & (ABRIR_LER | ABRIR_ESCREVER)) == (ABRIR_LER | ABRIR_ESCREVER)) flags = O_RDWR;
        else if (modo & ABRIR_ESCREVER) flags = O_WRONLY;
        else flags = O_RDONLY;
        if (modo & ABRIR_ACRESCENTAR) flags |= O_APPEND;
        __mlibc_fd_definir(fd, flags);
    }
    return fd;
}

int mlibc_read(int fd, void *buffer, unsigned count)
{
    return (int)read(fd, buffer, count);
}

int mlibc_write(int fd, const void *buffer, unsigned count)
{
    return (int)write(fd, buffer, count);
}

int mlibc_seek(int fd, int displacement, int origin)
{
    off_t r = lseek(fd, displacement, origin);
    return r < 0 ? -1 : (int)r;
}

int mlibc_close(int fd) { return close(fd); }

int mlibc_clock_get(unsigned long *ms)
{
    if (!ms) { errno = EFAULT; return -1; }
    *ms = (unsigned long)tempo_ms();
    return 0;
}

void mlibc_libc_log(const char *text)
{
    if (text) (void)registrar_texto(text);
}

void mlibc_libc_panic(const char *text)
{
    if (text) (void)registrar_texto(text);
    sair(1);
}

void *mlibc_anon_allocate(unsigned count) { return malloc(count); }
int mlibc_anon_free(void *pointer, unsigned count)
{
    (void)count;
    free(pointer);
    return 0;
}

int mlibc_futex_wait(void *uaddr, int value, int timeout_ms)
{
    (void)uaddr; (void)value; (void)timeout_ms;
    errno = ENOSYS;
    return -1;
}

int mlibc_futex_wake(void *uaddr, int count)
{
    (void)uaddr;
    if (count <= 0) return 0;
    /* Nao pode haver waiters sem suporte a threads/futexes. */
    return 0;
}

int mlibc_vm_map(void *address, unsigned count, void **result)
{
    (void)address; (void)count;
    if (result) *result = NULL;
    errno = ENOSYS;
    return -1;
}

int mlibc_vm_unmap(void *address, unsigned count)
{
    (void)address; (void)count;
    errno = ENOSYS;
    return -1;
}

int mlibc_tcb_set(void *tcb)
{
    (void)tcb;
    /* Sem threads nao ha TLS a instalar; errno e global por processo. */
    return 0;
}
