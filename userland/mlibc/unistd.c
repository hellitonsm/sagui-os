#include <string.h>
/* unistd.c - adaptacao POSIX das chamadas int 0x80 do SaguiOS. */
#include "internal.h"
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>

static unsigned char descritor_conhecido[MLIBC_DESCRITORES] = { 1, 1, 1 };
static int flags_descritor[MLIBC_DESCRITORES] = { O_RDONLY, O_WRONLY, O_WRONLY };

void __mlibc_fd_definir(int fd, int flags)
{
    if (fd < 0 || fd >= MLIBC_DESCRITORES) return;
    descritor_conhecido[fd] = 1;
    flags_descritor[fd] = flags;
}

void __mlibc_fd_esquecer(int fd)
{
    if (fd < 0 || fd >= MLIBC_DESCRITORES) return;
    descritor_conhecido[fd] = 0;
    flags_descritor[fd] = 0;
}

int __mlibc_fd_conhecido(int fd)
{
    return fd >= 0 && fd < MLIBC_DESCRITORES && descritor_conhecido[fd];
}

int __mlibc_fd_flags(int fd)
{
    return __mlibc_fd_conhecido(fd) ? flags_descritor[fd] : -1;
}

int open(const char *path, int flags, ...)
{
    const int permitidas = O_ACCMODE | O_CREAT | O_EXCL | O_NOCTTY |
                           O_TRUNC | O_APPEND | O_NONBLOCK | O_DSYNC | O_SYNC |
                           O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_LARGEFILE;
    int acesso = flags & O_ACCMODE;
    if (!path) { errno = EFAULT; return -1; }
    if ((flags & ~permitidas) || acesso == O_ACCMODE ||
        ((flags & O_EXCL) && !(flags & O_CREAT)) ||
        ((flags & O_TRUNC) && acesso == O_RDONLY)) {
        errno = EINVAL;
        return -1;
    }
    /* Nonblocking nao existe no kernel; sincronizacao de escrita ja e
     * sincrona e O_CLOEXEC e redundante (exec herda apenas fd 0..2). */
    if (flags & O_NONBLOCK) { errno = ENOTSUP; return -1; }

    if ((flags & O_EXCL) && (flags & O_CREAT)) {
        info_arquivo_t info;
        int r = info_arquivo(path, &info);
        if (r >= 0) { errno = EEXIST; return -1; }
        if (r != ERRO_NAO_EXISTE) return __mlibc_resultado(r);
    }
    if (flags & O_DIRECTORY) {
        info_arquivo_t info;
        int r = info_arquivo(path, &info);
        if (r < 0) return __mlibc_resultado(r);
        if (info.tipo != TIPO_PASTA) { errno = ENOTDIR; return -1; }
        /* O VFS nao expoe diretorios como descritores abertos. */
        errno = ENOTSUP;
        return -1;
    }

    int modo = 0;
    if (acesso == O_RDONLY || acesso == O_RDWR) modo |= ABRIR_LER;
    if (acesso == O_WRONLY || acesso == O_RDWR) modo |= ABRIR_ESCREVER;
    if (flags & O_CREAT)  modo |= ABRIR_CRIAR;
    if (flags & O_TRUNC)  modo |= ABRIR_TRUNCAR;
    if (flags & O_APPEND) modo |= ABRIR_ACRESCENTAR;

    int fd = abrir(path, modo);
    if (fd < 0) return __mlibc_resultado(fd);
    __mlibc_fd_definir(fd, acesso | (flags & O_APPEND));
    return fd;
}

int creat(const char *path, mode_t mode)
{
    (void)mode;
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
}

ssize_t read(int fd, void *buffer, size_t count)
{
    if (count > (size_t)SSIZE_MAX) { errno = EINVAL; return -1; }
    if (!count) return 0;
    if (!buffer) { errno = EFAULT; return -1; }
    int flags = __mlibc_fd_flags(fd);
    if (flags >= 0 && (flags & O_ACCMODE) == O_WRONLY) {
        errno = EBADF;
        return -1;
    }
    return (ssize_t)__mlibc_resultado_io(ler(fd, buffer, (unsigned)count));
}

ssize_t write(int fd, const void *buffer, size_t count)
{
    if (count > (size_t)SSIZE_MAX) { errno = EINVAL; return -1; }
    if (!count) return 0;
    if (!buffer) { errno = EFAULT; return -1; }
    int flags = __mlibc_fd_flags(fd);
    if (flags >= 0 && (flags & O_ACCMODE) == O_RDONLY) {
        errno = EBADF;
        return -1;
    }
    return (ssize_t)__mlibc_resultado_io(escrever(fd, buffer, (unsigned)count));
}

int close(int fd)
{
    int r = __mlibc_resultado(fechar(fd));
    if (r == 0) __mlibc_fd_esquecer(fd);
    return r;
}

off_t lseek(int fd, off_t offset, int whence)
{
    if (offset < (off_t)INT_MIN || offset > (off_t)INT_MAX) {
        errno = EOVERFLOW;
        return (off_t)-1;
    }
    int r = posicionar(fd, (int)offset, whence);
    if (r < 0) {
        if (r == ERRO_ACESSO) errno = ESPIPE;
        else __mlibc_resultado(r);
        return (off_t)-1;
    }
    return (off_t)r;
}

pid_t getpid(void) { return (pid_t)meu_pid(); }

int unlink(const char *path)
{
    if (!path) { errno = EFAULT; return -1; }
    return __mlibc_resultado(apagar(path));
}

int rmdir(const char *path)
{
    if (!path) { errno = EFAULT; return -1; }
    return __mlibc_resultado(remover_pasta(path));
}

int mkdir(const char *path, mode_t mode)
{
    (void)mode; /* o VFS nao implementa permissoes POSIX */
    if (!path) { errno = EFAULT; return -1; }
    return __mlibc_resultado(criar_pasta(path));
}

int chdir(const char *path)
{
    if (!path) { errno = EFAULT; return -1; }
    return __mlibc_resultado(mudar_pasta(path));
}

char *getcwd(char *buffer, size_t size)
{
    int allocated = 0;
    if (!buffer) {
        if (size) { errno = EINVAL; return NULL; }
        size = PATH_MAX;
        buffer = malloc(size);
        if (!buffer) return NULL;
        allocated = 1;
    }
    if (!size) { errno = EINVAL; return NULL; }
    int r = pasta_atual(buffer, (unsigned)size);
    if (r < 0) {
        if (r == ERRO_LIMITE) errno = ERANGE;
        else __mlibc_resultado(r);
        if (allocated) free(buffer);
        return NULL;
    }
    return buffer;
}

int access(const char *path, int mode)
{
    if (!path) { errno = EFAULT; return -1; }
    if (mode & ~(R_OK | W_OK | X_OK)) { errno = EINVAL; return -1; }
    info_arquivo_t info;
    int r = info_arquivo(path, &info);
    if (r < 0) return __mlibc_resultado(r);
    /* O VFS nao tem ACLs: arquivos normais sao legiveis/escreviveis para
     * qualquer processo; apenas o tipo programa recebe X_OK. */
    if ((mode & X_OK) && info.tipo != TIPO_PROGRAMA) {
        errno = EACCES;
        return -1;
    }
    return 0;
}

int pipe(int descriptors[2])
{
    if (!descriptors) { errno = EFAULT; return -1; }
    int local[2];
    int r = __mlibc_resultado(criar_pipe(local));
    if (r < 0) return -1;
    descriptors[0] = local[0];
    descriptors[1] = local[1];
    __mlibc_fd_definir(local[0], O_RDONLY);
    __mlibc_fd_definir(local[1], O_WRONLY);
    return 0;
}

int dup(int fd)
{
    int flags = __mlibc_fd_flags(fd);
    if (flags < 0) {
        /* Pede ao kernel validar tambem descritores criados fora desta shim. */
        int probe = duplicar(fd, fd);
        if (probe < 0) return __mlibc_resultado(probe);
        flags = O_RDWR;
    }
    for (int novo = 0; novo < MLIBC_DESCRITORES; novo++) {
        if (__mlibc_fd_conhecido(novo)) continue;
        int r = duplicar(fd, novo);
        if (r < 0) return __mlibc_resultado(r);
        __mlibc_fd_definir(novo, flags);
        return novo;
    }
    errno = EMFILE;
    return -1;
}

int dup2(int oldfd, int newfd)
{
    if (newfd < 0 || newfd >= MLIBC_DESCRITORES) { errno = EBADF; return -1; }
    int flags = __mlibc_fd_flags(oldfd);
    if (flags < 0) {
        int probe = duplicar(oldfd, oldfd);
        if (probe < 0) return __mlibc_resultado(probe);
        flags = O_RDWR;
    }
    int r = duplicar(oldfd, newfd);
    if (r < 0) return __mlibc_resultado(r);
    __mlibc_fd_definir(newfd, flags);
    return r;
}

int isatty(int fd)
{
    if (!__mlibc_fd_conhecido(fd)) {
        int probe = duplicar(fd, fd);
        if (probe < 0) { __mlibc_resultado(probe); return 0; }
    }
    if (fd >= STDIN_FILENO && fd <= STDERR_FILENO) return 1;
    errno = ENOTTY;
    return 0;
}

int fcntl(int fd, int command, ...)
{
    int arg = 0;
    if (command == F_DUPFD || command == F_SETFD || command == F_SETFL) {
        va_list ap;
        va_start(ap, command);
        arg = va_arg(ap, int);
        va_end(ap);
    }
    int flags = __mlibc_fd_flags(fd);
    if (flags < 0) {
        int probe = duplicar(fd, fd);
        if (probe < 0) return __mlibc_resultado(probe);
        flags = O_RDWR;
        __mlibc_fd_definir(fd, flags);
    }
    switch (command) {
    case F_GETFD:
        return 0; /* CLOEXEC nao tem efeito: o kernel so herda fd 0..2. */
    case F_SETFD:
        return (arg & ~FD_CLOEXEC) ? (errno = EINVAL, -1) : 0;
    case F_GETFL:
        return flags;
    case F_SETFL:
        if ((arg & O_APPEND) != (flags & O_APPEND) ||
            (arg & ~(O_ACCMODE | O_APPEND))) {
            errno = ENOTSUP;
            return -1;
        }
        return 0;
    case F_DUPFD:
        if (arg < 0 || arg >= MLIBC_DESCRITORES) { errno = EINVAL; return -1; }
        for (int novo = arg; novo < MLIBC_DESCRITORES; novo++) {
            if (__mlibc_fd_conhecido(novo)) continue;
            int r = duplicar(fd, novo);
            if (r < 0) return __mlibc_resultado(r);
            __mlibc_fd_definir(novo, flags);
            return r;
        }
        errno = EMFILE;
        return -1;
    default:
        errno = EINVAL;
        return -1;
    }
}

char *environ[] = { (char *)0 };

int execvp(const char *file, char *const argv[])
{
    /* O SaguiOS nao tem execve de arquivos host; nada a executar. */
    (void)file; (void)argv;
    errno = ENOSYS;
    return -1;
}

char *realpath(const char *caminho, char *buffer)
{
    char temporario[4096];
    if (!caminho) { errno = EINVAL; return NULL; }
    if (caminho[0] == '/') {
        if (strlen(caminho) >= sizeof temporario) { errno = ENAMETOOLONG; return NULL; }
        strcpy(temporario, caminho);
    } else {
        if (!getcwd(temporario, sizeof temporario)) return NULL;
        size_t n = strlen(temporario);
        size_t resto = strlen(caminho);
        if (n + 1 + resto >= sizeof temporario) { errno = ENAMETOOLONG; return NULL; }
        temporario[n] = '/';
        temporario[n + 1] = '\0';
        strcat(temporario, caminho);
    }
    /* Normaliza: remove ".", resolve "..". */
    char resultado[4096];
    resultado[0] = '/';
    resultado[1] = '\0';
    char *dst = resultado + 1;
    const char *p = temporario;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *e = p;
        while (*e && *e != '/') e++;
        size_t len = (size_t)(e - p);
        if (len == 1 && p[0] == '.') { p = e; continue; }
        if (len == 2 && p[0] == '.' && p[1] == '.') {
            if (dst > resultado + 1) {
                dst--;
                while (dst > resultado + 1 && dst[-1] != '/') dst--;
            }
            p = e;
            continue;
        }
        if ((size_t)(dst - resultado) + len + 1 >= sizeof resultado) {
            errno = ENAMETOOLONG;
            return NULL;
        }
        memcpy(dst, p, len);
        dst += len;
        *dst++ = '/';
        *dst = '\0';
        p = e;
    }
    if (dst > resultado + 1) dst--;
    *dst = '\0';
    size_t total = (size_t)(dst - resultado) + 1;
    if (!buffer) {
        buffer = malloc(total);
        if (!buffer) return NULL;
    }
    memcpy(buffer, resultado, total);
    return buffer;
}

