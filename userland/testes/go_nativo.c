/* Compila no anel 3, executa o ELF gerado e verifica bootstrap e bibliotecas. */
#include "sagui.h"

static int rodar(char *const args[])
{
    int codigo = -1;
    int pid = executar(args[0], args);
    if (pid < 0 || esperar(pid, &codigo) < 0) return -1;
    return codigo;
}

static int compilar(const char *origem, const char *destino)
{
    char *args[] = {"/bin/gosagui", "build", "-o", (char *)destino, (char *)origem, NULL};
    return rodar(args);
}

/* Redireciona a saida do filho para um pipe e devolve o que ele escreveu. */
static int capturar(char *const args[], char *buf, unsigned max, int *codigo)
{
    int fds[2];
    if (criar_pipe(fds) < 0) return -1;
    int salvo = abrir("/go/log", ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (salvo < 0) { fechar(fds[0]); fechar(fds[1]); return -1; }
    duplicar(fds[1], SAIDA);
    *codigo = rodar(args);
    duplicar(salvo, SAIDA);
    fechar(fds[1]);
    int n = ler(fds[0], buf, max - 1);
    fechar(fds[0]);
    fechar(salvo);
    if (n < 0) return -1;
    buf[n] = 0;
    return n;
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (compilar("/go/ola.go", "/go/ola") != 0) return 1;
    if (compilar("/go/biblioteca.go", "/go/biblioteca") != 0) return 2;

    char buf[256];
    int codigo = -1;
    char *ola[] = {"/go/ola", NULL};
    if (capturar(ola, buf, sizeof buf, &codigo) < 0) return 3;
    if (codigo != 0 || compara_texto(buf, "Ola, Go no SaguiOS!\n")) return 4;

    char *biblioteca[] = {"/go/biblioteca", NULL};
    if (capturar(biblioteca, buf, sizeof buf, &codigo) < 0) return 5;
    if (codigo != 0 || compara_texto(buf, "[init] Ola, bibliotecas!\ncom importacao\n")) return 6;

    /* Fontes invalidas nao podem destruir o executavel anterior. */
    const char *ruins[] = {
        "package main\nfunc main() { println(42) }\n",
        "package main\nfunc main() { println(\"x\") } lixo",
        "package main\nfunc main() { println(\"escape\\",
        "package main\n/* comentario aberto",
        "package main\nfunc main() { println(\"x\") print(\"y\") }",
        "package outro\nfunc main() {}",
        "package main\nfunc main() { println(\"x\\q\") }",
        "package main\nimport \"naoexiste\"\nfunc main() { println(\"x\") }",
        "package main\nimport \"fmt\"\nfunc main() { fmt.NaoExiste() }",
        "package main\nfunc main() { fmt.Println(\"x\") }",
        "package main\nimport f \"fmt\"\nfunc main() { fmt.Println(\"x\") }",
        "package main\nimport \"fmt\"\nfunc main() { println(\"x\") }",
        "package main\nimport \"sagui\"\nfunc main() { sagui.LimparTela(\"x\") }",
        "package main\nfunc main() {}\nfunc main() {}"
    };
    for (unsigned i = 0; i < sizeof ruins / sizeof ruins[0]; i++) {
        int fd = abrir("/go/ruim.go", ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
        if (fd < 0 || escrever_tudo(fd, ruins[i], tam_texto(ruins[i])) < 0) return 7;
        fechar(fd);
        if (compilar("/go/ruim.go", "/go/ola") != 1) return 8;
    }

    if (capturar(ola, buf, sizeof buf, &codigo) < 0) return 9;
    if (codigo != 0 || compara_texto(buf, "Ola, Go no SaguiOS!\n")) return 10;

    registrar("[GO] bootstrap, importacao e ELF validados\n");
    return 0;
}
