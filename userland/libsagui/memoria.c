/* memoria.c - aloca/libera para os programas.
 *
 * A área de dados do programa é uma faixa contínua que cresce com a chamada
 * aumentar_memoria. Dentro dela os blocos ficam um atrás do outro, cada um
 * com um cabeçalho que diz o tamanho e se está livre: para achar o próximo
 * bloco basta somar o tamanho. A busca é "o primeiro que couber", e blocos
 * livres vizinhos são juntados durante a própria busca. Para os programas
 * pequenos do SaguiOS isso é simples e suficiente. */
#include "sagui.h"

#define ALINHAMENTO 8
#define CRESCER_MIN (16 * 1024)

typedef struct {
    uint32_t tamanho;   /* do bloco inteiro, cabeçalho incluído */
    uint32_t livre;
} bloco_t;

static uint8_t *inicio, *fim;

static bloco_t *proximo(bloco_t *b) { return (bloco_t *)((uint8_t *)b + b->tamanho); }

static bool crescer(uint32_t pelo_menos)
{
    if (pelo_menos > 0xFFFFFFFFu - 4095u) return false;
    uint32_t n = pelo_menos < CRESCER_MIN ? CRESCER_MIN : (pelo_menos + 4095u) & ~4095u;
    if (n < pelo_menos) return false;
    uint8_t *antigo = aumentar_memoria(n);
    if (!antigo) return false;
    if (!inicio) inicio = fim = antigo;
    /* A área nova vira um bloco livre no fim (que se junta ao anterior, se
     * ele também estiver livre, na próxima busca). */
    bloco_t *b = (bloco_t *)fim;
    b->tamanho = n;
    b->livre = 1;
    fim += n;
    return true;
}

void *aloca(size_t n)
{
    if (n == 0) n = 1;
    if (n > 0xFFFFFFFFu - sizeof(bloco_t) - (ALINHAMENTO - 1)) return NULL;
    uint32_t precisa = (uint32_t)((n + sizeof(bloco_t) + ALINHAMENTO - 1) & ~(ALINHAMENTO - 1));
    for (int tentativa = 0; tentativa < 2; tentativa++) {
        for (bloco_t *b = (bloco_t *)inicio; (uint8_t *)b < fim; b = proximo(b)) {
            if (!b->livre) continue;
            while ((uint8_t *)proximo(b) < fim && proximo(b)->livre)   /* junta vizinhos livres */
                b->tamanho += proximo(b)->tamanho;
            if (b->tamanho < precisa) continue;
            if (b->tamanho - precisa >= sizeof(bloco_t) + ALINHAMENTO) {   /* divide */
                bloco_t *resto = (bloco_t *)((uint8_t *)b + precisa);
                resto->tamanho = b->tamanho - precisa;
                resto->livre = 1;
                b->tamanho = precisa;
            }
            b->livre = 0;
            return b + 1;
        }
        if (!crescer(precisa)) return NULL;
    }
    return NULL;
}

void *aloca_zerado(size_t n)
{
    void *p = aloca(n);
    if (p) memset(p, 0, n);
    return p;
}

void libera(void *p)
{
    if (!p) return;
    ((bloco_t *)p - 1)->livre = 1;
}

void *realoca(void *p, size_t n)
{
    if (!p) return aloca(n);
    bloco_t *b = (bloco_t *)p - 1;
    size_t atual = b->tamanho - sizeof(bloco_t);
    if (n <= atual) return p;
    void *novo = aloca(n);
    if (!novo) return NULL;
    memcpy(novo, p, atual);
    libera(p);
    return novo;
}
