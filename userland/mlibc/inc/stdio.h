/* stdio.h - espelho mlibc adaptado ao SaguiOS.
 * Sem buffer: FILE guarda so o descritor (o kernel bufferiza o terminal).
 * EOF = -1, como no mlibc. */
#ifndef MLIBC_STDIO_H
#define MLIBC_STDIO_H
#include "sagui.h"
typedef struct { int fd; } FILE;
extern FILE *stdin, *stdout, *stderr;
#define EOF (-1)
int    printf(const char *fmt, ...);
int    fprintf(FILE *f, const char *fmt, ...);
int    sprintf(char *buf, const char *fmt, ...);
int    snprintf(char *buf, unsigned max, const char *fmt, ...);
int    puts(const char *s);
int    putchar(int c);
int    fputs(const char *s, FILE *f);
int    fputc(int c, FILE *f);
FILE  *fopen(const char *caminho, const char *modo);
int    fclose(FILE *f);
unsigned fread(void *buf, unsigned tam, unsigned n, FILE *f);
unsigned fwrite(const void *buf, unsigned tam, unsigned n, FILE *f);
char  *fgets(char *buf, int max, FILE *f);
int    fgetc(FILE *f);
int    fflush(FILE *f);
#endif
