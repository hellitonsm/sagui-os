/* math.h - o SaguiOS nao tem FPU (CR0.EM); este header declara as
 * funcoes basicas cujo suporte e proibido em tempo de execucao (ver
 * userland/mlibc/float.c). */
#ifndef SAGUI_MLIBC_MATH_H
#define SAGUI_MLIBC_MATH_H

double ldexp(double x, int exponent);
float ldexpf(float x, int exponent);
long double ldexpl(long double x, int exponent);

#endif
