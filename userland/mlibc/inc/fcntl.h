/* fcntl.h - espelho mlibc adaptado ao SaguiOS.
 * GetIDs musicais do mlibc/Linux; mlibc.c traduz para ABRIR_* do SaguiOS. */
#ifndef MLIBC_FCNTL_H
#define MLIBC_FCNTL_H
#include "sagui.h"
#define O_RDONLY   0
#define O_WRONLY   1
#define O_RDWR     2
#define O_CREAT    0100   /* 64 */
#define O_TRUNC    01000  /* 512 */
#define O_APPEND   02000  /* 1024 */
#define O_EXCL     0200   /* 128 */
#define SEEK_SET   0
#define SEEK_CUR   1
#define SEEK_END   2
int open(const char *caminho, int flags, ...);
int fcntl(int fd, int cmd, ...);
#endif
