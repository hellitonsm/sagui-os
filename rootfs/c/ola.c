/* Exemplo do subconjunto C aceito por /bin/csagui. */
int soma(int esquerda, int direita) {
    return esquerda + direita;
}

int main(void) {
    int i = 0;
    int total = 0;

    while (i < 4) {
        total = total + i;
        i = i + 1;
    }

    if (soma(total, 1) == 7) {
        puts("[C] compilado dentro do SaguiOS");
        return 0;
    }
    puts("[C] falha no exemplo");
    return 1;
}
