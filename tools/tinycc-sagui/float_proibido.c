/* float_proibido.c - o SaguiOS nao salva o estado da FPU (CR0.EM) e nao ha
 * ponto flutuante em lugar nenhum. O tinycc usa long double por dentro
 * (constantes literais), entao os helpers de IEEE double nao existem na
 * toolchain i686-elf (sem soft-float no libgcc). Aqui eles sao substituidos
 * por stubs que abortam com mensagem clara: programas em C que precisem de
 * float/double devem ser compilados no hospedeiro com mlibc. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static _Noreturn void float_proibido(void)
{
    static const char msg[] = "tinycc: literais de ponto flutuante nao suportados no SaguiOS\n";
    (void)write(2, msg, sizeof msg - 1);
    exit(3);
}

double __adddf3(double a, double b) { (void)a; (void)b; float_proibido(); }
double __subdf3(double a, double b) { (void)a; (void)b; float_proibido(); }
double __muldf3(double a, double b) { (void)a; (void)b; float_proibido(); }
double __divdf3(double a, double b) { (void)a; (void)b; float_proibido(); }
int    __eqdf2(double a, double b)  { (void)a; (void)b; float_proibido(); }
int    __nedf2(double a, double b)  { (void)a; (void)b; float_proibido(); }
int    __ltdf2(double a, double b)  { (void)a; (void)b; float_proibido(); }
int    __ledf2(double a, double b)  { (void)a; (void)b; float_proibido(); }
int    __gtdf2(double a, double b)  { (void)a; (void)b; float_proibido(); }
int    __gedf2(double a, double b)  { (void)a; (void)b; float_proibido(); }
double __floatunsidf(unsigned int n) { (void)n; float_proibido(); }
double __floatunsidi(float n) { (void)n; float_proibido(); }
float  __truncdfsf2(double n) { (void)n; float_proibido(); }

