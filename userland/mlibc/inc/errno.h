/* errno.h - espelho mlibc adaptado ao SaguiOS.
 * O SaguiOS devolve erros negativos da chamada; o mlibc usa o global
 * errno com valores POSIX. mlibc.c traduz ERRO_* para estes codigos. */
#ifndef MLIBC_ERRNO_H
#define MLIBC_ERRNO_H
extern int errno;
#define EPERM   1
#define ENOENT  2
#define EIO     5
#define EBADF   9
#define EAGAIN  11
#define ENOMEM  12
#define EEXIST  17
#define ENODEV  19
#define EINVAL  22
#define ENOTDIR 20
#define EISDIR  21
#define EPIPE   32
#define ENOSPC  28
#define ESPIPE  29
#define ENAMETOOLONG 36
#define ENOSYS  38
#endif
