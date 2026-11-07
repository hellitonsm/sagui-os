/* Glob do SaguiOS - sem expansao de curingas no sistema de arquivos. O
 * glob do host e usado pelo tinycc so para procurar libgcc_s.so.*, que nao
 * existe aqui; por isso o retorno e sempre vazio. */
#ifndef SAGUI_MLIBC_GLOB_H
#define SAGUI_MLIBC_GLOB_H
#include <stddef.h>

#define GLOB_ERR      0x0001
#define GLOB_NOSORT   0x0004
#define GLOB_NOCHECK  0x0010
#define GLOB_NOESCAPE 0x1000
#define GLOB_NOMATCH  3
#define GLOB_NOSYS    4

typedef struct {
    size_t    gl_pathc;
    size_t    gl_matchc;
    size_t    gl_offs;
    int       gl_flags;
    char    **gl_pathv;
    void     *gl_dirname;
} glob_t;

static inline int glob(const char *padrao, int flags,
                       int (*erro)(const char *, int), glob_t *resultado)
{
    (void)padrao; (void)flags; (void)erro;
    if (!resultado) return GLOB_NOMATCH;
    resultado->gl_pathc = 0;
    resultado->gl_pathv = (char **)0;
    resultado->gl_offs = 0;
    return GLOB_NOMATCH;
}

static inline void globfree(glob_t *resultado) { (void)resultado; }

#endif
