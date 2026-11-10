/* hello-tcc.c - programa C "de verdade" compilado dentro do SaguiOS pelo
 * tinycc. Usa stdio, string e funcoes com loop do libc nativo:
 *
 *   tinycc /tcc/hello-tcc.c     # gera ./hello-tcc (sem -o)
 *   ./hello-tcc
 */
#include <stdio.h>
#include <string.h>

static int fatorial(int n)
{
    int r = 1;
    for (int i = 2; i <= n; i++) r = r * i;
    return r;
}

int main(void)
{
    char buf[64];
    const char *nome = "SaguiOS";
    int soma = 0;
    for (int i = 1; i <= 10; i++) soma += i;

    snprintf(buf, sizeof buf, "ola, %s, compilado pelo tinycc!", nome);
    puts(buf);
    printf("soma 1..10 = %d, 7! = %d\n", soma, fatorial(7));
    printf("numero: %d, hex: 0x%x\n", 42, 42);
    return 0;
}
