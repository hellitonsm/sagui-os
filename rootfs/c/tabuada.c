/* Exemplo do subconjunto C aceito por /bin/csagui: soma 1..n com laço e
 * desvio, do mesmo jeito que /c/ola.c. Compile e rode dentro do SaguiOS:
 *
 *   csagui build -o /tmp/tabuada /c/tabuada.c
 *   /tmp/tabuada
 */
int soma(int n) {
    int i = 1;
    int total = 0;

    while (i <= n) {
        total = total + i;
        i = i + 1;
    }
    return total;
}

int main(void) {
    if (soma(10) == 55) {
        puts("[C] soma 1..10 = 55, compilado dentro do SaguiOS");
        return 0;
    }
    puts("[C] falha no exemplo");
    return 1;
}
