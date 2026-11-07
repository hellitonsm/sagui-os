/* stdio.c - streams POSIX sem buffer e formatacao inteira C99 do SaguiOS. */
#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#define STREAM_READ   0x01
#define STREAM_WRITE  0x02
#define STREAM_OWNED  0x04
#define STREAM_STATIC 0x08

struct __sagui_FILE {
    int fd;
    int flags;
    int eof;
    int error;
    int has_pushback;
    unsigned char pushback;
};

static FILE stream_stdin  = { STDIN_FILENO,  STREAM_READ | STREAM_STATIC,  0, 0, 0, 0 };
static FILE stream_stdout = { STDOUT_FILENO, STREAM_WRITE | STREAM_STATIC, 0, 0, 0, 0 };
static FILE stream_stderr = { STDERR_FILENO, STREAM_WRITE | STREAM_STATIC, 0, 0, 0, 0 };
FILE *stdin = &stream_stdin;
FILE *stdout = &stream_stdout;
FILE *stderr = &stream_stderr;

static int stream_valido(FILE *stream)
{
    if (!stream) { errno = EINVAL; return 0; }
    if (stream->fd < 0) { errno = EBADF; stream->error = 1; return 0; }
    return 1;
}

static int modo_fopen(const char *mode, int *open_flags, int *stream_flags)
{
    if (!mode || !mode[0]) return 0;
    int first = mode[0];
    int flags, sf;
    if (first == 'r') { flags = O_RDONLY; sf = STREAM_READ; }
    else if (first == 'w') { flags = O_WRONLY | O_CREAT | O_TRUNC; sf = STREAM_WRITE; }
    else if (first == 'a') { flags = O_WRONLY | O_CREAT | O_APPEND; sf = STREAM_WRITE; }
    else return 0;

    int plus = 0, binary = 0, exclusive = 0;
    for (const char *p = mode + 1; *p; p++) {
        if (*p == '+' && !plus) plus = 1;
        else if (*p == 'b' && !binary) binary = 1;
        else if (*p == 'x' && !exclusive && first == 'w') exclusive = 1;
        else return 0;
    }
    (void)binary; /* texto e binario sao identicos no SaguiOS. */
    if (plus) {
        flags = (flags & ~(O_ACCMODE)) | O_RDWR;
        sf = STREAM_READ | STREAM_WRITE;
    }
    if (exclusive) flags |= O_EXCL;
    *open_flags = flags;
    *stream_flags = sf;
    return 1;
}

FILE *fopen(const char *path, const char *mode)
{
    int oflags, sflags;
    if (!path || !modo_fopen(mode, &oflags, &sflags)) {
        errno = EINVAL;
        return NULL;
    }
    int fd = open(path, oflags, 0666);
    if (fd < 0) return NULL;
    FILE *stream = malloc(sizeof(FILE));
    if (!stream) {
        int saved = errno;
        (void)close(fd);
        errno = saved;
        return NULL;
    }
    stream->fd = fd;
    stream->flags = sflags | STREAM_OWNED;
    stream->eof = 0;
    stream->error = 0;
    stream->has_pushback = 0;
    stream->pushback = 0;
    return stream;
}

FILE *fdopen(int fd, const char *mode)
{
    int oflags, sflags;
    if (!modo_fopen(mode, &oflags, &sflags)) { errno = EINVAL; return NULL; }
    int actual = fcntl(fd, F_GETFL);
    if (actual < 0) return NULL;
    int access = actual & O_ACCMODE;
    if ((sflags & STREAM_READ) && access == O_WRONLY) { errno = EINVAL; return NULL; }
    if ((sflags & STREAM_WRITE) && access == O_RDONLY) { errno = EINVAL; return NULL; }
    FILE *stream = malloc(sizeof(FILE));
    if (!stream) return NULL;
    stream->fd = fd;
    stream->flags = sflags | STREAM_OWNED;
    stream->eof = 0;
    stream->error = 0;
    stream->has_pushback = 0;
    stream->pushback = 0;
    return stream;
}

FILE *freopen(const char *path, const char *mode, FILE *stream)
{
    if (!stream) { errno = EINVAL; return NULL; }
    fflush(stream);
    int oflags, sflags;
    if (!path || !modo_fopen(mode, &oflags, &sflags)) {
        errno = EINVAL;
        stream->fd = -1;
        stream->error = 1;
        return NULL;
    }
    int newfd = open(path, oflags, 0666);
    if (newfd < 0) {
        stream->fd = -1;
        stream->error = 1;
        return NULL;
    }
    int fd = stream->fd;
    if (fd < 0) {
        stream->fd = newfd; /* stream estava quebrada: ganha o descritor novo */
    } else {
        int r = dup2(newfd, fd);
        (void)close(newfd);
        if (r < 0) { stream->fd = -1; stream->error = 1; return NULL; }
    }
    int keep = stream->flags & (STREAM_STATIC | STREAM_OWNED);
    stream->flags = sflags | keep;
    stream->eof = 0;
    stream->error = 0;
    stream->has_pushback = 0;
    return stream;
}

int fclose(FILE *stream)
{
    if (!stream) { errno = EINVAL; return EOF; }
    int owned = !(stream->flags & STREAM_STATIC);
    int fd = stream->fd;
    stream->fd = -1;    stream->has_pushback = 0;
    int r = close(fd);
    if (owned) free(stream);
    return r < 0 ? EOF : 0;
}

int fflush(FILE *stream)
{
    if (!stream) return 0; /* todos os streams sao sem buffer */
    return stream_valido(stream) ? 0 : EOF;
}

size_t fread(void *buffer, size_t size, size_t count, FILE *stream)
{
    if (!size || !count) return 0;
    if (!stream_valido(stream)) return 0;
    if (!(stream->flags & STREAM_READ)) { errno = EBADF; stream->error = 1; return 0; }
    if (count > (size_t)-1 / size) { errno = EOVERFLOW; stream->error = 1; return 0; }
    size_t total = size * count;
    if (!buffer) { errno = EFAULT; stream->error = 1; return 0; }

    unsigned char *dst = buffer;
    size_t done = 0;
    if (stream->eof && !stream->has_pushback) return 0;
    if (stream->has_pushback && total) {
        dst[done++] = stream->pushback;
        stream->has_pushback = 0;
        stream->eof = 0;
    }
    while (done < total) {
        ssize_t r = read(stream->fd, dst + done, total - done);
        if (r < 0) { stream->error = 1; break; }
        if (r == 0) { stream->eof = 1; break; }
        done += (size_t)r;
    }
    return done / size;
}

size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream)
{
    if (!size || !count) return 0;
    if (!stream_valido(stream)) return 0;
    if (!(stream->flags & STREAM_WRITE)) { errno = EBADF; stream->error = 1; return 0; }
    if (count > (size_t)-1 / size) { errno = EOVERFLOW; stream->error = 1; return 0; }
    size_t total = size * count;
    if (!buffer) { errno = EFAULT; stream->error = 1; return 0; }

    const unsigned char *src = buffer;
    size_t done = 0;
    while (done < total) {
        ssize_t r = write(stream->fd, src + done, total - done);
        if (r < 0) { stream->error = 1; break; }
        if (r == 0) { errno = EIO; stream->error = 1; break; }
        done += (size_t)r;
    }
    return done / size;
}

int fgetc(FILE *stream)
{
    if (!stream_valido(stream)) return EOF;
    if (!(stream->flags & STREAM_READ)) { errno = EBADF; stream->error = 1; return EOF; }
    if (stream->has_pushback) {
        stream->has_pushback = 0;
        stream->eof = 0;
        return stream->pushback;
    }
    if (stream->eof) return EOF;
    unsigned char c;
    ssize_t r = read(stream->fd, &c, 1);
    if (r < 0) { stream->error = 1; return EOF; }
    if (!r) { stream->eof = 1; return EOF; }
    stream->eof = 0;
    return (int)c;
}

int getc(FILE *stream) { return fgetc(stream); }
int getchar(void) { return fgetc(stdin); }

int ungetc(int character, FILE *stream)
{
    if (character == EOF) return EOF;
    if (!stream_valido(stream)) return EOF;
    if (!(stream->flags & STREAM_READ) || stream->has_pushback) return EOF;
    stream->pushback = (unsigned char)character;
    stream->has_pushback = 1;
    stream->eof = 0;
    return stream->pushback;
}

int fputc(int character, FILE *stream)
{
    if (!stream_valido(stream)) return EOF;
    if (!(stream->flags & STREAM_WRITE)) { errno = EBADF; stream->error = 1; return EOF; }
    unsigned char c = (unsigned char)character;
    if (write(stream->fd, &c, 1) != 1) { stream->error = 1; return EOF; }
    return c;
}

int putc(int character, FILE *stream) { return fputc(character, stream); }
int putchar(int character) { return fputc(character, stdout); }

int fputs(const char *text, FILE *stream)
{
    if (!text || !stream_valido(stream)) { if (!text) errno = EFAULT; return EOF; }
    size_t n = strlen(text);
    return fwrite(text, 1, n, stream) == n ? 0 : EOF;
}

int puts(const char *text)
{
    if (fputs(text, stdout) == EOF || fputc('\n', stdout) == EOF) return EOF;
    return 0;
}

char *fgets(char *buffer, int size, FILE *stream)
{
    if (!buffer || size <= 0) { errno = EINVAL; return NULL; }
    if (!stream_valido(stream)) return NULL;
    if (size == 1) { buffer[0] = '\0'; return buffer; }
    int n = 0;
    while (n < size - 1) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (!n) return NULL;
            break;
        }
        buffer[n++] = (char)c;
        if (c == '\n') break;
    }
    buffer[n] = '\0';
    return buffer;
}

int feof(FILE *stream) { return stream ? stream->eof : 0; }
int ferror(FILE *stream) { return stream ? stream->error : 0; }
void clearerr(FILE *stream) { if (stream) { stream->eof = 0; stream->error = 0; } }
int fileno(FILE *stream) { return stream_valido(stream) ? stream->fd : -1; }

int fseek(FILE *stream, long offset, int whence)
{
    if (!stream_valido(stream)) return -1;
    if (lseek(stream->fd, (off_t)offset, whence) < 0) return -1;
    stream->eof = 0;
    stream->has_pushback = 0;
    return 0;
}

long ftell(FILE *stream)
{
    if (!stream_valido(stream)) return -1L;
    off_t position = lseek(stream->fd, 0, SEEK_CUR);
    if (position < 0) return -1L;
    if (stream->has_pushback) {
        if (!position) { errno = EINVAL; return -1L; }
        position--;
    }
    return (long)position;
}

void rewind(FILE *stream)
{
    if (fseek(stream, 0, SEEK_SET) == 0) clearerr(stream);
}

int setvbuf(FILE *stream, char *buffer, int mode, size_t size)
{
    (void)buffer; (void)size;
    if (!stream_valido(stream)) return -1;
    if (mode != _IONBF && mode != _IOFBF && mode != _IOLBF) { errno = EINVAL; return -1; }
    if (mode != _IONBF) { errno = ENOTSUP; return -1; }
    return 0;
}

void setbuf(FILE *stream, char *buffer)
{
    (void)setvbuf(stream, buffer, _IONBF, 0);
}

int remove(const char *path)
{
    if (!path) { errno = EFAULT; return -1; }
    info_arquivo_t info;
    int r = info_arquivo(path, &info);
    if (r < 0) return __mlibc_resultado(r);
    if (info.tipo == TIPO_PASTA) return __mlibc_resultado(remover_pasta(path));
    return __mlibc_resultado(apagar(path));
}

int rename(const char *old_path, const char *new_path)
{
    if (!old_path || !new_path) { errno = EFAULT; return -1; }
    return __mlibc_resultado(renomear(old_path, new_path));
}

/* ---------- printf family: flags, width, precision, integer lengths ---------- */
typedef struct {
    int to_fd;
    int fd;
    char *destination;
    size_t capacity;
    size_t count;
    char pending[128];
    size_t pending_count;
    int failed;
} format_sink_t;

static int sink_flush(format_sink_t *sink)
{
    size_t sent = 0;
    while (sent < sink->pending_count) {
        ssize_t r = write(sink->fd, sink->pending + sent, sink->pending_count - sent);
        if (r < 0) { sink->failed = 1; return -1; }
        if (!r) { errno = EIO; sink->failed = 1; return -1; }
        sent += (size_t)r;
    }
    sink->pending_count = 0;
    return 0;
}

static int sink_char(format_sink_t *sink, char c)
{
    if (sink->failed) return -1;
    if (sink->count >= (size_t)INT_MAX) {
        errno = EOVERFLOW;
        sink->failed = 1;
        return -1;
    }
    if (sink->to_fd) {
        sink->pending[sink->pending_count++] = c;
        if (sink->pending_count == sizeof sink->pending && sink_flush(sink) < 0) return -1;
    } else if (sink->capacity && sink->count < sink->capacity - 1) {
        sink->destination[sink->count] = c;
    }
    sink->count++;
    return 0;
}

static int sink_repeat(format_sink_t *sink, char c, int count)
{
    while (count-- > 0)
        if (sink_char(sink, c) < 0) return -1;
    return 0;
}

static int formato_numero(format_sink_t *sink, unsigned long long value,
                          int negative, unsigned base, int upper,
                          int width, int precision, int left, int zero,
                          int plus, int space, int alternate, int pointer)
{
    char digits[64];
    int nd = 0;
    unsigned long long original = value;
    do {
        unsigned rem = 0;
        value = __mlibc_divmod_u64(value, base, &rem);
        digits[nd++] = (char)((rem < 10) ? ('0' + rem) :
                              ((upper ? 'A' : 'a') + rem - 10));
    } while (value);
    if (precision == 0 && original == 0) nd = 0;

    char sign = 0;
    if (negative) sign = '-';
    else if (plus) sign = '+';
    else if (space) sign = ' ';

    int leading_zeroes = precision > nd ? precision - nd : 0;
    char prefix[2];
    int np = 0;
    if (pointer) { prefix[0] = '0'; prefix[1] = 'x'; np = 2; }
    else if (alternate && base == 16 && original) {
        prefix[0] = '0'; prefix[1] = upper ? 'X' : 'x'; np = 2;
    } else if (alternate && base == 8 && (!nd || (leading_zeroes == 0 && digits[nd - 1] != '0'))) {
        prefix[0] = '0'; np = 1;
    }

    int content = nd + leading_zeroes + np + (sign != 0);
    int padding = width > content ? width - content : 0;
    int use_zero = zero && !left && precision < 0;
    if (!left && !use_zero && sink_repeat(sink, ' ', padding) < 0) return -1;
    if (sign && sink_char(sink, sign) < 0) return -1;
    for (int i = 0; i < np; i++) if (sink_char(sink, prefix[i]) < 0) return -1;
    if (use_zero && sink_repeat(sink, '0', padding) < 0) return -1;
    if (sink_repeat(sink, '0', leading_zeroes) < 0) return -1;
    while (nd-- > 0) if (sink_char(sink, digits[nd]) < 0) return -1;
    if (left && sink_repeat(sink, ' ', padding) < 0) return -1;
    return 0;
}

enum { LEN_DEFAULT, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_Z, LEN_T, LEN_J, LEN_CAPL };

static int emitir_texto(format_sink_t *sink, const char *text, size_t length,
                        int width, int left)
{
    int padding = width > 0 && (size_t)width > length ? width - (int)length : 0;
    if (!left && sink_repeat(sink, ' ', padding) < 0) return -1;
    for (size_t i = 0; i < length; i++)
        if (sink_char(sink, text[i]) < 0) return -1;
    if (left && sink_repeat(sink, ' ', padding) < 0) return -1;
    return 0;
}

static int ler_decimal(const char **format, int *result)
{
    unsigned v = 0;
    const char *p = *format;
    for (; *p >= '0' && *p <= '9'; p++) {
        unsigned d = (unsigned)(*p - '0');
        if (v > ((unsigned)INT_MAX - d) / 10u) { errno = EOVERFLOW; return -1; }
        v = v * 10u + d;
    }
    *format = p;
    *result = (int)v;
    return 0;
}

static int formatar(format_sink_t *sink, const char *format, va_list ap)
{
    if (!format) { errno = EINVAL; return -1; }
    for (const char *p = format; *p; ) {
        if (*p != '%') {
            if (sink_char(sink, *p++) < 0) return -1;
            continue;
        }
        p++;
        int left = 0, plus = 0, space = 0, alternate = 0, zero = 0;
        int flags_done = 0;
        while (!flags_done) {
            switch (*p) {
            case '-': left = 1; p++; break;
            case '+': plus = 1; p++; break;
            case ' ': space = 1; p++; break;
            case '#': alternate = 1; p++; break;
            case '0': zero = 1; p++; break;
            default: flags_done = 1; break;
            }
        }
        int width = 0;
        if (*p == '*') {
            width = va_arg(ap, int);
            p++;
            if (width < 0) {
                left = 1;
                if (width == INT_MIN) { errno = EOVERFLOW; return -1; }
                width = -width;
            }
        } else if (*p >= '0' && *p <= '9') {
            if (ler_decimal(&p, &width) < 0) return -1;
        }
        int precision = -1;
        if (*p == '.') {
            p++;
            precision = 0;
            if (*p == '*') {
                precision = va_arg(ap, int);
                p++;
                if (precision < 0) precision = -1;
            } else if (*p >= '0' && *p <= '9') {
                if (ler_decimal(&p, &precision) < 0) return -1;
            }
        }
        int length = LEN_DEFAULT;
        if (p[0] == 'h' && p[1] == 'h') { length = LEN_HH; p += 2; }
        else if (p[0] == 'h') { length = LEN_H; p++; }
        else if (p[0] == 'l' && p[1] == 'l') { length = LEN_LL; p += 2; }
        else if (p[0] == 'l') { length = LEN_L; p++; }
        else if (p[0] == 'z') { length = LEN_Z; p++; }
        else if (p[0] == 't') { length = LEN_T; p++; }
        else if (p[0] == 'j') { length = LEN_J; p++; }
        else if (p[0] == 'L') { length = LEN_CAPL; p++; }

        char conversion = *p;
        if (conversion) p++;
        switch (conversion) {
        case '%':
            if (sink_char(sink, '%') < 0) return -1;
            break;
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (emitir_texto(sink, &c, 1, width, left) < 0) return -1;
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            size_t n = precision >= 0 ? strnlen(s, (size_t)precision) : strlen(s);
            if (emitir_texto(sink, s, n, width, left) < 0) return -1;
            break;
        }
        case 'd': case 'i': {
            long long v;
            switch (length) {
            case LEN_HH: v = (signed char)va_arg(ap, int); break;
            case LEN_H:  v = (short)va_arg(ap, int); break;
            case LEN_L:  v = va_arg(ap, long); break;
            case LEN_LL: case LEN_J: v = va_arg(ap, long long); break;
            case LEN_Z: case LEN_T: v = va_arg(ap, ptrdiff_t); break;
            default: v = va_arg(ap, int); break;
            }
            int negative = v < 0;
            unsigned long long magnitude = negative ? 0ULL - (unsigned long long)v : (unsigned long long)v;
            if (formato_numero(sink, magnitude, negative, 10, 0, width, precision,
                               left, zero, plus, space, 0, 0) < 0) return -1;
            break;
        }
        case 'u': case 'o': case 'x': case 'X': {
            unsigned long long v;
            switch (length) {
            case LEN_HH: v = (unsigned char)va_arg(ap, unsigned int); break;
            case LEN_H:  v = (unsigned short)va_arg(ap, unsigned int); break;
            case LEN_L:  v = va_arg(ap, unsigned long); break;
            case LEN_LL: case LEN_J: v = va_arg(ap, unsigned long long); break;
            case LEN_Z: v = va_arg(ap, size_t); break;
            case LEN_T: v = (unsigned long)va_arg(ap, ptrdiff_t); break;
            default: v = va_arg(ap, unsigned int); break;
            }
            unsigned base = conversion == 'o' ? 8u :
                            (conversion == 'x' || conversion == 'X') ? 16u : 10u;
            if (formato_numero(sink, v, 0, base, conversion == 'X', width,
                               precision, left, zero, 0, 0, alternate, 0) < 0) return -1;
            break;
        }
        case 'p': {
            unsigned long long v = (unsigned long long)(unsigned long)va_arg(ap, void *);
            if (formato_numero(sink, v, 0, 16, 0, width, precision, left, zero,
                               0, 0, 0, 1) < 0) return -1;
            break;
        }
        case 'n': {
            int n = (int)sink->count;
            switch (length) {
            case LEN_HH: *va_arg(ap, signed char *) = (signed char)n; break;
            case LEN_H:  *va_arg(ap, short *) = (short)n; break;
            case LEN_L:  *va_arg(ap, long *) = (long)n; break;
            case LEN_LL: case LEN_J: *va_arg(ap, long long *) = (long long)n; break;
            case LEN_Z: *va_arg(ap, ptrdiff_t *) = (ptrdiff_t)n; break;
            case LEN_T: *va_arg(ap, ptrdiff_t *) = (ptrdiff_t)n; break;
            default: *va_arg(ap, int *) = n; break;
            }
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
            /* FPU/IEEE formatting deliberately excluded by the SaguiOS ABI. */
            errno = ENOSYS;
            sink->failed = 1;
            return -1;
        case '\0':
            if (sink_char(sink, '%') < 0) return -1;
            return 0;
        default:
            if (sink_char(sink, '%') < 0) return -1;
            if (sink_char(sink, conversion) < 0) return -1;
            break;
        }
    }
    return 0;
}

static int formatar_fd(FILE *stream, const char *format, va_list ap)
{
    if (!stream_valido(stream)) return -1;
    if (!(stream->flags & STREAM_WRITE)) { errno = EBADF; stream->error = 1; return -1; }
    format_sink_t sink = { 1, stream->fd, NULL, 0, 0, {0}, 0, 0 };
    int formatted = formatar(&sink, format, ap);
    int flushed = sink_flush(&sink);
    if (formatted < 0 || flushed < 0) {
        stream->error = 1;
        return -1;
    }
    return (int)sink.count;
}

static int formatar_buffer(char *buffer, size_t size, const char *format, va_list ap)
{
    if (size && !buffer) { errno = EFAULT; return -1; }
    format_sink_t sink = { 0, -1, buffer, size, 0, {0}, 0, 0 };
    int r = formatar(&sink, format, ap);
    if (size) buffer[sink.count < size ? sink.count : size - 1] = '\0';
    if (r < 0) return -1;
    return (int)sink.count;
}

int vfprintf(FILE *stream, const char *format, va_list arguments)
{
    return formatar_fd(stream, format, arguments);
}
int vprintf(const char *format, va_list arguments) { return vfprintf(stdout, format, arguments); }
int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments)
{
    return formatar_buffer(buffer, size, format, arguments);
}
int vsprintf(char *buffer, const char *format, va_list arguments)
{
    if (!buffer) { errno = EFAULT; return -1; }
    return formatar_buffer(buffer, (size_t)-1, format, arguments);
}

int fprintf(FILE *stream, const char *format, ...)
{
    va_list ap; va_start(ap, format);
    int r = vfprintf(stream, format, ap);
    va_end(ap);
    return r;
}
int printf(const char *format, ...)
{
    va_list ap; va_start(ap, format);
    int r = vprintf(format, ap);
    va_end(ap);
    return r;
}
int snprintf(char *buffer, size_t size, const char *format, ...)
{
    va_list ap; va_start(ap, format);
    int r = vsnprintf(buffer, size, format, ap);
    va_end(ap);
    return r;
}
int sprintf(char *buffer, const char *format, ...)
{
    va_list ap; va_start(ap, format);
    int r = vsprintf(buffer, format, ap);
    va_end(ap);
    return r;
}
