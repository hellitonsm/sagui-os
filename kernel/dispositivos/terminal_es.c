/* terminal_es.c - o terminal visto pelos programas.
 *
 * Saída: o texto vai para a tela (e para a serial, que é o log dos testes).
 *
 * Entrada, modo linha (padrão): o kernel monta a linha enquanto a pessoa
 * digita, com eco e Backspace, e só a entrega ao programa quando vem o
 * Enter. Ctrl+D numa linha vazia significa "fim do arquivo" (o "wc" lendo
 * do teclado, por exemplo, para aí).
 *
 * Entrada, modo bruto: cada tecla é entregue na hora, sem eco. Teclas sem
 * caractere (setas, Home...) viram dois bytes: ESC e uma letra (abi.h). O
 * shell usa este modo para ter histórico e completar com Tab.
 *
 * Quando não há o que entregar, o leitor dorme no canal do teclado e a
 * chamada é repetida quando chegar uma tecla (D010). */
#include "terminal_es.h"
#include "abi.h"
#include "console.h"
#include "processo.h"
#include "sistema.h"
#include "teclado.h"

#define MAX_LINHA 256

static int modo = TERM_LINHA;
static char linha[MAX_LINHA];
static uint32_t tam_linha, entregue;
static bool linha_pronta;
static int pendente = -1;   /* segundo byte de uma tecla especial no modo bruto */

/* Quem pôs o terminal em modo bruto (o shell editando uma linha). Se outro
 * processo escrever nesse meio-tempo (um "contador &", por exemplo), o texto
 * começa numa linha nova e o shell recebe ESC 'R' para redesenhar o prompt e
 * o que já estava digitado, em vez de as duas coisas se misturarem. */
static int dono_bruto = -1;
static bool redesenhar;

static void eco(char c)
{
    console_char(c);
    serial_char(c);
}

int32_t terminal_escrever(const char *origem, uint32_t n)
{
    if (modo == TERM_BRUTO && atual && atual->pid != dono_bruto && n > 0) {
        if (!redesenhar && console_coluna() != 0)
            eco('\n');
        redesenhar = true;
        acordar(teclado_canal());
    }
    for (uint32_t i = 0; i < n; i++)
        eco(origem[i]);
    console_mostrar();
    return (int32_t)n;
}

void terminal_modo(int m)
{
    modo = m;
    dono_bruto = (m == TERM_BRUTO && atual) ? atual->pid : -1;
    redesenhar = false;
    tam_linha = entregue = 0;
    linha_pronta = false;
    pendente = -1;
}

void terminal_limpar(void)
{
    console_limpar();
    console_mostrar();
}

static char letra_especial(int t)
{
    switch (t) {
    case TECLA_CIMA: return TECLA_B_CIMA;       case TECLA_BAIXO: return TECLA_B_BAIXO;
    case TECLA_DIREITA: return TECLA_B_DIREITA; case TECLA_ESQUERDA: return TECLA_B_ESQUERDA;
    case TECLA_HOME: return TECLA_B_HOME;       case TECLA_END: return TECLA_B_END;
    case TECLA_DELETE: return TECLA_B_DELETE;   case TECLA_PGUP: return TECLA_B_PGUP;
    case TECLA_PGDN: return TECLA_B_PGDN;
    default: return 0;
    }
}

static int32_t ler_bruto(char *destino, uint32_t n)
{
    uint32_t k = 0;
    if (redesenhar && pendente < 0) {
        redesenhar = false;
        destino[k++] = 27;
        pendente = TECLA_B_REDESENHAR;
    }
    while (k < n) {
        if (pendente >= 0) { destino[k++] = (char)pendente; pendente = -1; continue; }
        int t = teclado_tentar();
        if (t < 0) break;
        if (t < 0x100) { destino[k++] = (char)t; continue; }
        char l = letra_especial(t);
        if (t == TECLA_ESC) { destino[k++] = 27; pendente = 27; continue; }   /* Esc sozinho = 27, 27 */
        if (!l) continue;
        destino[k++] = 27;
        pendente = (uint8_t)l;
    }
    if (k) return (int32_t)k;
    bloquear(teclado_canal());
    return SIS_REPETIR;
}

/* Modo bruto, sem bloquear: o proximo byte da tecla, ou -1 se nao ha nenhuma.
 * E o mesmo estado da leitura bloqueante: ESC + letra para teclas especiais,
 * dois bytes devolvidos em chamadas seguidas. So vale no modo bruto (o shell
 * depende disso); fora dele devolve -1. */
int32_t terminal_tentar(void)
{
    if (modo != TERM_BRUTO) return -1;
    if (redesenhar && pendente < 0) {
        redesenhar = false;
        pendente = TECLA_B_REDESENHAR;
        return 27;
    }
    if (pendente >= 0) {
        int t = pendente;
        pendente = -1;
        return (int32_t)t;
    }
    int t = teclado_tentar();
    if (t < 0) return -1;
    if (t < 0x100) return (int32_t)t;
    char l = letra_especial(t);
    if (t == TECLA_ESC) { pendente = 27; return 27; }
    if (!l) return -1;
    pendente = (uint8_t)l;
    return 27;
}

int32_t terminal_ler(char *destino, uint32_t n)
{
    if (n == 0) return 0;
    if (modo == TERM_BRUTO) return ler_bruto(destino, n);

    while (!linha_pronta) {
        int t = teclado_tentar();
        if (t < 0) {
            bloquear(teclado_canal());
            return SIS_REPETIR;
        }
        if (t == '\n') {
            eco('\n');
            linha[tam_linha++] = '\n';
            linha_pronta = true;
        } else if (t == '\b') {
            if (tam_linha) { tam_linha--; eco('\b'); }
        } else if (t == 4) {                 /* Ctrl+D */
            if (tam_linha == 0) return 0;    /* fim do arquivo */
            linha_pronta = true;
        } else if (t == 3) {                 /* Ctrl+C: descarta a linha */
            terminal_escrever("^C\n", 3);
            tam_linha = 0;
        } else if (t >= 0x20 && t < 0x100 && t != 0x7F && tam_linha < MAX_LINHA - 1) {
            linha[tam_linha++] = (char)t;
            eco((char)t);
        }
    }
    uint32_t k = 0;
    while (k < n && entregue < tam_linha)
        destino[k++] = linha[entregue++];
    if (entregue == tam_linha) {
        tam_linha = entregue = 0;
        linha_pronta = false;
    }
    return (int32_t)k;
}
