/* unistd.h - subset POSIX implementado pelas chamadas do SaguiOS. */
#ifndef SAGUI_MLIBC_UNISTD_H
#define SAGUI_MLIBC_UNISTD_H
#include <sys/types.h>

#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

#define F_OK  0
#define X_OK  1
#define W_OK  2
#define R_OK  4

ssize_t read(int fd, void *buffer, size_t count);
ssize_t write(int fd, const void *buffer, size_t count);
int     close(int fd);
off_t   lseek(int fd, off_t offset, int whence);
pid_t   getpid(void);
unsigned int sleep(unsigned int seconds);
int     usleep(unsigned int microseconds);
int     unlink(const char *path);
int     rmdir(const char *path);
int     mkdir(const char *path, mode_t mode);
int     chdir(const char *path);
char   *getcwd(char *buffer, size_t size);
int     access(const char *path, int mode);
int     pipe(int descriptors[2]);
int     dup(int fd);
int     dup2(int oldfd, int newfd);
int     isatty(int fd);

#endif
