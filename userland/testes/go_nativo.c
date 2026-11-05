/* Compila no anel 3, executa o ELF gerado e verifica a saida por pipe. */
#include "sagui.h"
static int rodar(char *const args[])
{
    int codigo = -1;
    int pid = executar(args[0], args);
    if (pid < 0 || esperar(pid, &codigo) < 0) return -1;
    return codigo;
}
int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    char *compilar[] = {"/bin/gosagui", "build", "-o", "/go/ola", "/go/ola.go", NULL};
    char *invalido[] = {"/bin/gosagui", "build", "-o", "/go/ola", "/go/invalido.go", NULL};
    if (rodar(compilar) != 0) return 1;
    /* Um erro nao pode destruir o executavel anterior. */
    if (rodar(invalido) != 1) return 2;
    const char *ruins[] = {
        "package main\nfunc main() { println(42) }\n",
        "package main\nfunc main() { println(\"x\") } lixo",
        "package main\nfunc main() { println(\"escape\\",
        "package main\n/* comentario aberto",
        "package main\nfunc main() { println(\"x\") print(\"y\") }",
        "package outro\nfunc main() {}",
        "package main\nfunc main() { println(\"x\\q\") }"
    };
    char *rejeitar[] = {"/bin/gosagui", "build", "-o", "/go/ola", "/go/ruim.go", NULL};
    for (unsigned i = 0; i < sizeof ruins / sizeof ruins[0]; i++) {
        int fd = abrir("/go/ruim.go", ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
        if (fd < 0 || escrever_tudo(fd, ruins[i], tam_texto(ruins[i])) < 0) return 7;
        fechar(fd);
        if (rodar(rejeitar) != 1) return 8;
    }
    int fds[2];
    if (criar_pipe(fds) < 0) return 3;
    /* executar herda descritores; o pai restaura a saida logo depois. */
    int salvo = abrir("/go/log", ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (salvo < 0) return 4;
    duplicar(fds[1], SAIDA);
    char *executavel[] = {"/go/ola", NULL};
    int codigo = rodar(executavel);
    duplicar(salvo, SAIDA);
    fechar(fds[1]);
    char buf[128];
    int n = ler(fds[0], buf, sizeof buf - 1);
    fechar(fds[0]); fechar(salvo);
    if (n < 0) return 5;
    buf[n] = 0;
    if (codigo != 0 || compara_texto(buf, "Ola, Go no SaguiOS!\n")) return 6;
    registrar("[GO] compilacao nativa, ELF e saida validados\n");
    return 0;
}
