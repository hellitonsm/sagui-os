/* fcntl.h - descritores e flags aceitos pela camada POSIX do SaguiOS. */
#ifndef SAGUI_MLIBC_FCNTL_H
#define SAGUI_MLIBC_FCNTL_H
#include <sys/types.h>

#define O_RDONLY     0x0000
#define O_WRONLY     0x0001
#define O_RDWR       0x0002
#define O_ACCMODE    0x0003
#define O_CREAT      0x0040
#define O_EXCL       0x0080
#define O_NOCTTY     0x0100
#define O_TRUNC      0x0200
#define O_APPEND     0x0400
#define O_NONBLOCK   0x0800
#define O_NDELAY     O_NONBLOCK
#define O_DSYNC      0x1000
#define O_SYNC       0x101000
#define O_RSYNC      O_SYNC
#define O_DIRECTORY  0x10000
#define O_NOFOLLOW   0x20000
#define O_CLOEXEC    0x80000
#define O_LARGEFILE  0x100000
#define O_BINARY     0

#define SEEK_SET     0
#define SEEK_CUR     1
#define SEEK_END     2

#define F_DUPFD      0
#define F_GETFD      1
#define F_SETFD      2
#define F_GETFL      3
#define F_SETFL      4
#define FD_CLOEXEC   1

int open(const char *path, int flags, ...);
int creat(const char *path, mode_t mode);
int fcntl(int fd, int command, ...);

#endif
