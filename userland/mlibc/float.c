/* float.c - suporte minimo a ponto flutuante no SaguiOS.
 *
 * O kernel desliga a FPU (CR0.EM) e o libc nao tem rotinas IEEE; qualquer
 * operacao de ponto flutuante e ilegal aqui. Estes simbolos existem porque
 * o tinycc os referencia na compilacao de literais: em vez de uma resposta
 * silenciosamente errada, abortam com mensagem clara. Este arquivo so e
 * puxado do arquivo para programas que usam ponto flutuante. */
#include "internal.h"
#include <string.h>
#include <unistd.h>

static void float_proibido(void)
{
    /* Mensagem mesma do float_proibido.c do tinycc. */
    static const char msg[] =
        "tinycc: literais de ponto flutuante nao suportados no SaguiOS\n";
    (void)write(STDERR_FILENO, msg, sizeof msg - 1);
    sair(3);
}

double strtod(const char *texto, char **fim)
{
    (void)texto; (void)fim;
    float_proibido();
}

float strtof(const char *texto, char **fim)
{
    (void)texto; (void)fim;
    float_proibido();
}

long double strtold(const char *texto, char **fim)
{
    (void)texto; (void)fim;
    float_proibido();
}

double ldexp(double x, int exponent)
{
    (void)x; (void)exponent;
    float_proibido();
}

float ldexpf(float x, int exponent)
{
    (void)x; (void)exponent;
    float_proibido();
}

long double ldexpl(long double x, int exponent)
{
    (void)x; (void)exponent;
    float_proibido();
}

/* float (IEEE 754 binary32) -> double (binary64). Puramente bit a bit,
 * sem FPU: conversao exata, sem arredondamento. */
double __extendsfdf2(float valor)
{
    union { float f; unsigned u; } entrada;
    union { unsigned long long u; double d; } saida;
    entrada.f = valor;
    unsigned sinal = (entrada.u >> 31) & 1u;
    unsigned exp   = (entrada.u >> 23) & 0xffu;
    unsigned frac  = entrada.u & 0x7fffffu;
    unsigned long long biased;
    unsigned long long mant;

    if (exp == 0xffu) {
        /* infinito ou NaN */
        saida.u = ((unsigned long long)sinal << 63) |
                  0x7ff0000000000000ull | ((unsigned long long)frac << 29);
        return saida.d;
    }
    if (exp == 0u) {
        if (frac == 0u) {
            saida.u = (unsigned long long)sinal << 63;
            return saida.d;
        }
        /* subnormal: normaliza a mantissa */
        int shift = 0;
        while ((frac & 0x800000u) == 0u) {
            frac <<= 1;
            shift++;
        }
        frac &= 0x7fffffu;
        biased = (unsigned long long)((-126 - shift) + 1023);
        mant = frac;
    } else {
        biased = (unsigned long long)((int)exp - 127 + 1023);
        mant = frac;
    }
    saida.u = ((unsigned long long)sinal << 63) | (biased << 52) | (mant << 29);
    return saida.d;
}
