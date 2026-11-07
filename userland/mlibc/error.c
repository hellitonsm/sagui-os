/* error.c - traducao dos erros do kernel para errno POSIX. */
#include "internal.h"
#include <sagui.h>
#include <string.h>
#include <unistd.h>

int errno;

void __mlibc_erro(int codigo)
{
    errno = codigo;
}

static int traduzir_erro(int r)
{
    switch (r) {
    case ERRO_INVALIDO:       return EINVAL;
    case ERRO_NAO_EXISTE:    return ENOENT;
    case ERRO_JA_EXISTE:     return EEXIST;
    case ERRO_SEM_MEMORIA:   return ENOMEM;
    case ERRO_ENDERECO:      return EFAULT;
    case ERRO_NAO_E_PASTA:   return ENOTDIR;
    case ERRO_E_PASTA:       return EISDIR;
    case ERRO_NAO_VAZIA:     return ENOTEMPTY;
    case ERRO_LIMITE:        return EMFILE;
    case ERRO_DESCRITOR:     return EBADF;
    case ERRO_PIPE_QUEBRADO: return EPIPE;
    case ERRO_SEM_PROCESSO:  return ESRCH;
    case ERRO_FORMATO:       return ENOEXEC;
    case ERRO_ACESSO:        return EACCES;
    case ERRO_OCUPADO:       return EBUSY;
    default:                 return EIO;
    }
}

int __mlibc_resultado(int resultado)
{
    if (resultado < 0) {
        errno = traduzir_erro(resultado);
        return -1;
    }
    return resultado;
}

/* O kernel usa ERRO_ACESSO quando um descritor nao foi aberto para o tipo
 * de operacao pedido. Para read/write POSIX isso e EBADF, nao EACCES. */
int __mlibc_resultado_io(int resultado)
{
    if (resultado == ERRO_ACESSO) {
        errno = EBADF;
        return -1;
    }
    return __mlibc_resultado(resultado);
}

/* Mensagens estaticas: strerror nao aloca e nao depende de locale. */
char *strerror(int e)
{
    switch (e) {
    case 0:         return "Success";
    case EPERM:     return "Operation not permitted";
    case ENOENT:    return "No such file or directory";
    case ESRCH:     return "No such process";
    case EINTR:     return "Interrupted system call";
    case EIO:       return "Input/output error";
    case ENXIO:     return "No such device or address";
    case E2BIG:     return "Argument list too long";
    case ENOEXEC:   return "Exec format error";
    case EBADF:     return "Bad file descriptor";
    case ECHILD:    return "No child processes";
    case EAGAIN:    return "Resource temporarily unavailable";
    case ENOMEM:    return "Cannot allocate memory";
    case EACCES:    return "Permission denied";
    case EFAULT:    return "Bad address";
    case EBUSY:     return "Device or resource busy";
    case EEXIST:    return "File exists";
    case EXDEV:     return "Cross-device link";
    case ENODEV:    return "No such device";
    case ENOTDIR:   return "Not a directory";
    case EISDIR:    return "Is a directory";
    case EINVAL:    return "Invalid argument";
    case ENFILE:    return "System file table is full";
    case EMFILE:    return "Too many open files";
    case ENOTTY:    return "Inappropriate ioctl for device";
    case EFBIG:     return "File too large";
    case ENOSPC:    return "No space left on device";
    case ESPIPE:    return "Illegal seek";
    case EROFS:     return "Read-only file system";
    case EPIPE:     return "Broken pipe";
    case EDOM:      return "Numerical argument out of domain";
    case ERANGE:    return "Numerical result out of range";
    case ENAMETOOLONG:return "File name too long";
    case ENOSYS:    return "Function not implemented";
    case ENOTEMPTY: return "Directory not empty";
    case EOVERFLOW: return "Value too large for defined data type";
    case ENOTSUP:   return "Operation not supported";
    default:        return "Unknown error";
    }
}

void perror(const char *prefix)
{
    int saved = errno;
    const char *message = strerror(saved);
    if (prefix && *prefix) {
        (void)write(STDERR_FILENO, prefix, strlen(prefix));
        (void)write(STDERR_FILENO, ": ", 2);
    }
    (void)write(STDERR_FILENO, message, strlen(message));
    (void)write(STDERR_FILENO, "\n", 1);
}

unsigned long long __mlibc_divmod_u64(unsigned long long value,
                                      unsigned divisor,
                                      unsigned *remainder)
{
    unsigned long long quotient = 0;
    unsigned r = 0;
    if (!divisor) {
        if (remainder) *remainder = 0;
        return 0;
    }
    for (int bit = 63; bit >= 0; bit--) {
        r = (r << 1) | (unsigned)((value >> bit) & 1ULL);
        if (r >= divisor) {
            r -= divisor;
            quotient |= 1ULL << bit;
        }
    }
    if (remainder) *remainder = r;
    return quotient;
}
/* __sagui_assert_fail - fim abrupto num assert do mlibc/tinycc. */
void __sagui_assert_fail(const char *expressao, const char *arquivo,
                         int linha)
{
    (void)write(STDERR_FILENO, "assert falhou: ", 15);
    if (expressao) (void)write(STDERR_FILENO, expressao, strlen(expressao));
    (void)write(STDERR_FILENO, " em ", 4);
    if (arquivo) (void)write(STDERR_FILENO, arquivo, strlen(arquivo));
    {
        unsigned value = (unsigned)linha;
        char digits[10];
        unsigned i = 0;
        (void)write(STDERR_FILENO, ":", 1);
        if (!value) {
            (void)write(STDERR_FILENO, "0", 1);
        } else {
            while (value) { digits[i++] = (char)('0' + value % 10); value /= 10; }
            while (i) (void)write(STDERR_FILENO, &digits[--i], 1);
        }
    }
    (void)write(STDERR_FILENO, "\n", 1);
    sair(70);
}