/* sys/mman.h - mapeamento de memoria. O kernel do SaguiOS nao mapeia
 * memoria em enderecos escolhidos pelo programa; aqui e so um stub que
 * falha, porque o tinycc so inclui este header no caminho de -run. */
#ifndef SAGUI_MLIBC_SYS_MMAN_H
#define SAGUI_MLIBC_SYS_MMAN_H
#include <sys/types.h>
#include <stddef.h>

#define PROT_NONE   0
#define PROT_READ   1
#define PROT_WRITE  2
#define PROT_EXEC   4
#define PROT_RWX    7

#define MAP_SHARED     0x01
#define MAP_PRIVATE    0x02
#define MAP_ANON       0x20
#define MAP_ANONYMOUS  MAP_ANON
#define MAP_FIXED      0x10
#define MAP_FAILED     ((void *)-1)

static inline void *mmap(void *endereco, size_t tamanho, int protecao,
                         int flags, int fd, off_t deslocamento)
{
    (void)endereco; (void)tamanho; (void)protecao; (void)flags;
    (void)fd; (void)deslocamento;
    return MAP_FAILED;
}

static inline int munmap(void *endereco, size_t tamanho)
{
    (void)endereco; (void)tamanho;
    return 0;
}

static inline int mprotect(void *endereco, size_t tamanho, int protecao)
{
    (void)endereco; (void)tamanho; (void)protecao;
    return 0;
}

#endif
