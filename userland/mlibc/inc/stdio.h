/* stdio.h - E/S POSIX sem buffering, adequada aos syscalls do SaguiOS.
 *
 * FILE e opaco; cada operacao chega imediatamente ao descritor do kernel.
 * A biblioteca nao implementa streams wide nem conversoes de ponto flutuante
 * no printf (o alvo e compilado sem FPU). */
#ifndef SAGUI_MLIBC_STDIO_H
#define SAGUI_MLIBC_STDIO_H
#include <sys/types.h>
#include <stdarg.h>

#ifndef EOF
#define EOF (-1)
#endif
#define BUFSIZ       512
#define FOPEN_MAX    16
#define FILENAME_MAX 128
#define _IONBF       0
#define _IOFBF       1
#define _IOLBF       2
#ifndef SEEK_SET
#define SEEK_SET     0
#define SEEK_CUR     1
#define SEEK_END     2
#endif

typedef struct __sagui_FILE FILE;
typedef long fpos_t;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int   printf(const char *format, ...);
int   fprintf(FILE *stream, const char *format, ...);
int   sprintf(char *buffer, const char *format, ...);
int   snprintf(char *buffer, size_t size, const char *format, ...);
int   vprintf(const char *format, va_list arguments);
int   vfprintf(FILE *stream, const char *format, va_list arguments);
int   vsprintf(char *buffer, const char *format, va_list arguments);
int   vsnprintf(char *buffer, size_t size, const char *format, va_list arguments);

int   puts(const char *text);
int   putchar(int character);
int   fputs(const char *text, FILE *stream);
int   fputc(int character, FILE *stream);
int   putc(int character, FILE *stream);
int   getchar(void);
int   getc(FILE *stream);
int   fgetc(FILE *stream);
int   ungetc(int character, FILE *stream);

FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
int   fclose(FILE *stream);
int   fflush(FILE *stream);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
char *fgets(char *buffer, int size, FILE *stream);
int   feof(FILE *stream);
int   ferror(FILE *stream);
void  clearerr(FILE *stream);
int   fileno(FILE *stream);
int   fseek(FILE *stream, long offset, int whence);
long  ftell(FILE *stream);
void  rewind(FILE *stream);
int   setvbuf(FILE *stream, char *buffer, int mode, size_t size);
void  setbuf(FILE *stream, char *buffer);

int remove(const char *path);
int rename(const char *old_path, const char *new_path);
void perror(const char *prefix);

#endif
