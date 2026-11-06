/* unistd.h - espelho mlibc adaptado ao SaguiOS. */
#ifndef MLIBC_UNISTD_H
#define MLIBC_UNISTD_H
#include "sagui.h"
int          close(int fd);
int          read(int fd, void *buf, unsigned n);
int          write(int fd, const void *buf, unsigned n);
int          getpid(void);
int          sleep(unsigned segundos);
int          usleep(unsigned usec);
int          unlink(const char *caminho);
int          rmdir(const char *caminho);
int          mkdir(const char *caminho, unsigned modo);
#endif
