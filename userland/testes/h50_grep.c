/* h50_grep.c - teste do grep no convidado (H50).
 *
 * Roda /bin/grep com a saída (e os erros) num pipe, confere o texto e o
 * código de saída, e imprime [ok]/[FALHA] na serial.  A entrada padrão pode
 * vir de um texto conhecido, para os casos de pipe. */
#include "sagui.h"
#include "verifica.h"

/* Roda o programa com a entrada, a saída e os erros em pipes.  Devolve o
 * tamanho da saída (saída + erros) e o código do programa. */
static int capturar(char *const args[], const char *entrada,
                    char *buf, unsigned max, int *codigo)
{
    int out[2], in[2];
    int tem_entrada = (entrada != NULL);
    int g0 = -1, g1, g2, pid;
    unsigned total = 0;

    *codigo = -1;
    if (criar_pipe(out) < 0) return -1;
    if (tem_entrada && criar_pipe(in) < 0) {
        fechar(out[0]);
        fechar(out[1]);
        return -1;
    }

    g1 = duplicar(SAIDA, 14);
    g2 = duplicar(ERROS, 15);
    if (tem_entrada) g0 = duplicar(ENTRADA, 13);
    if (g1 < 0 || g2 < 0 || (tem_entrada && g0 < 0)) {
        fechar(out[0]); fechar(out[1]);
        if (tem_entrada) { fechar(in[0]); fechar(in[1]); }
        if (g1 >= 0) fechar(g1);
        if (g2 >= 0) fechar(g2);
        if (g0 >= 0) fechar(g0);
        return -1;
    }

    duplicar(out[1], SAIDA);
    duplicar(out[1], ERROS);
    if (tem_entrada) duplicar(in[0], ENTRADA);

    pid = executar(args[0], args);

    duplicar(g1, SAIDA);
    duplicar(g2, ERROS);
    fechar(g1);
    fechar(g2);
    if (tem_entrada) {
        duplicar(g0, ENTRADA);
        fechar(g0);
    }
    fechar(out[1]);
    if (tem_entrada) {
        unsigned n = (unsigned)tam_texto(entrada);
        if (n) escrever(in[1], entrada, n);
        fechar(in[1]);
    }

    /* Lê até o fim antes de esperar: a saída pode ser maior que o pipe. */
    while (total + 1 < max) {
        int n = ler(out[0], buf + total, max - 1 - total);
        if (n <= 0) break;
        total += (unsigned)n;
    }
    buf[total] = 0;
    fechar(out[0]);

    if (pid >= 0) {
        int status = -1;
        if (esperar(pid, &status) == pid) *codigo = status;
    }
    return (int)total;
}

static void caso(const char *nome, char *const args[], const char *entrada,
                 int rc_esperado, const char *saida_esperada)
{
    char buf[2048];
    int rc = -1;
    int n = capturar(args, entrada, buf, sizeof buf, &rc);
    if (n >= 0 && rc == rc_esperado && compara_texto(buf, saida_esperada) == 0) {
        imprimir("  [ok] %s\n", nome);
        return;
    }
    imprimir("  [FALHA] %s (codigo %d, saida \"%s\")\n", nome, rc, buf);
    falhas_teste++;
}

static void contem_caso(const char *nome, char *const args[], const char *entrada,
                        int rc_esperado, const char *pedaco)
{
    char buf[2048];
    int rc = -1;
    int n = capturar(args, entrada, buf, sizeof buf, &rc);
    size_t k = tam_texto(pedaco);
    int achou = 0;
    if (n >= 0 && rc == rc_esperado)
        for (const char *p = buf; *p; p++)
            if (compara_texto_n(p, pedaco, k) == 0) { achou = 1; break; }
    if (achou) {
        imprimir("  [ok] %s\n", nome);
        return;
    }
    imprimir("  [FALHA] %s (codigo %d, saida \"%s\")\n", nome, rc, buf);
    falhas_teste++;
}

static int criar(const char *caminho, const char *texto, unsigned n)
{
    int fd = abrir(caminho, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd < 0) return -1;
    int r = (n > 0) ? escrever(fd, texto, n) : 0;
    fechar(fd);
    return r;
}

static int criar_texto(const char *caminho, const char *texto)
{
    return criar(caminho, texto, (unsigned)tam_texto(texto));
}

#define BIN "bin.dat"

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    char binario[] = { 'a', 0, 'b', '\n' };

    imprimir("Teste do grep (H50), processo %d\n", meu_pid());

    criar_texto("/tmp/g1.txt", "banana\nAbacaxi\ncaju\nbanana split\n");
    criar_texto("/tmp/g2.txt", "nada\nbanana\n");
    criar_texto("/tmp/g3.txt", "an\nana\nbanana\n");
    criar_texto("/tmp/g4.txt", "a.c\nabc\n");
    criar_texto("/tmp/g5.txt", "nada\n");
    criar_texto("/tmp/g6.txt", "x\nm\ny\nz\nm\nw\n");
    criar_texto("/tmp/g7.txt", "aa\nab\naaa\n");
    criar_texto("/tmp/pat.txt", "banana\ncaju\n");
    criar("/tmp/bin.dat", binario, sizeof binario);
    criar_pasta("/tmp/sub");
    criar_pasta("/tmp/sub/skip");
    criar_texto("/tmp/sub/x.txt", "banana\n");
    criar_texto("/tmp/sub/skip/y.txt", "banana\n");

    /* Casamento básico, BRE e ERE. */
    {
        char *a[] = { "/bin/grep", "banana", "/tmp/g1.txt", NULL };
        caso("BRE: uma palavra", a, NULL, 0, "banana\nbanana split\n");
    }
    {
        char *a[] = { "/bin/grep", "ba(na)+", "/tmp/g1.txt", NULL };
        caso("BRE: ( ) e + literais", a, NULL, 1, "");
    }
    {
        char *a[] = { "/bin/grep", "a\\{2\\}", "/tmp/g7.txt", NULL };
        caso("BRE: intervalo \\{2\\}", a, NULL, 0, "aa\naaa\n");
    }
    {
        char *a[] = { "/bin/grep", "-E", "^[A-Z]", "/tmp/g1.txt", NULL };
        caso("ERE: classe e âncora", a, NULL, 0, "Abacaxi\n");
    }
    {
        char *a[] = { "/bin/grep", "-E", "ba(na)+", "/tmp/g1.txt", NULL };
        caso("ERE: grupo e +", a, NULL, 0, "banana\nbanana split\n");
    }
    {
        char *a[] = { "/bin/grep", "-F", "a.c", "/tmp/g4.txt", NULL };
        caso("-F: ponto literal", a, NULL, 0, "a.c\n");
    }

    /* Opções de seleção. */
    {
        char *a[] = { "/bin/grep", "-i", "ABACAXI", "/tmp/g1.txt", NULL };
        caso("-i", a, NULL, 0, "Abacaxi\n");
    }
    {
        char *a[] = { "/bin/grep", "-v", "banana", "/tmp/g1.txt", NULL };
        caso("-v", a, NULL, 0, "Abacaxi\ncaju\n");
    }
    {
        char *a[] = { "/bin/grep", "-w", "an", "/tmp/g3.txt", NULL };
        caso("-w: palavra inteira", a, NULL, 0, "an\n");
    }
    {
        char *a[] = { "/bin/grep", "-w", "ana", "/tmp/g3.txt", NULL };
        caso("-w: não casa dentro de banana", a, NULL, 0, "ana\n");
    }
    {
        char *a[] = { "/bin/grep", "-x", "banana", "/tmp/g1.txt", NULL };
        caso("-x: linha inteira", a, NULL, 0, "banana\n");
    }
    {
        char *a[] = { "/bin/grep", "-e", "caju", "-e", "Abacaxi", "/tmp/g1.txt", NULL };
        caso("-e repetido", a, NULL, 0, "Abacaxi\ncaju\n");
    }
    {
        char *a[] = { "/bin/grep", "-f", "/tmp/pat.txt", "/tmp/g1.txt", NULL };
        caso("-f: padrões de arquivo", a, NULL, 0, "banana\ncaju\nbanana split\n");
    }

    /* Prefixos. */
    {
        char *a[] = { "/bin/grep", "-n", "banana", "/tmp/g1.txt", NULL };
        caso("-n", a, NULL, 0, "1:banana\n4:banana split\n");
    }
    {
        char *a[] = { "/bin/grep", "-b", "caju", "/tmp/g1.txt", NULL };
        caso("-b: deslocamento", a, NULL, 0, "15:caju\n");
    }
    {
        char *a[] = { "/bin/grep", "-n", "-b", "-o", "caju", "/tmp/g1.txt", NULL };
        caso("-n -b -o", a, NULL, 0, "3:15:caju\n");
    }
    {
        char *a[] = { "/bin/grep", "-n", "-T", "banana", "/tmp/g1.txt", NULL };
        caso("-T: tab depois do prefixo", a, NULL, 0, "1:\tbanana\n4:\tbanana split\n");
    }

    /* Contagem, listas e silêncio. */
    {
        char *a[] = { "/bin/grep", "-c", "banana", "/tmp/g1.txt", NULL };
        caso("-c", a, NULL, 0, "2\n");
    }
    {
        char *a[] = { "/bin/grep", "-o", "an", "/tmp/g1.txt", NULL };
        caso("-o: só o trecho", a, NULL, 0, "an\nan\nan\nan\n");
    }
    {
        char *a[] = { "/bin/grep", "-l", "banana", "/tmp/g1.txt", "/tmp/g5.txt", NULL };
        caso("-l: arquivos com", a, NULL, 0, "/tmp/g1.txt\n");
    }
    {
        char *a[] = { "/bin/grep", "-L", "banana", "/tmp/g1.txt", "/tmp/g5.txt", NULL };
        caso("-L: arquivos sem", a, NULL, 0, "/tmp/g5.txt\n");
    }
    {
        char *a[] = { "/bin/grep", "-q", "banana", "/tmp/g1.txt", NULL };
        caso("-q: achou", a, NULL, 0, "");
    }
    {
        char *a[] = { "/bin/grep", "-q", "nada", "/tmp/g1.txt", NULL };
        caso("-q: não achou", a, NULL, 1, "");
    }

    /* -m e contexto. */
    {
        char *a[] = { "/bin/grep", "-m1", "banana", "/tmp/g1.txt", NULL };
        caso("-m1", a, NULL, 0, "banana\n");
    }
    {
        char *a[] = { "/bin/grep", "-m1", "-c", "banana", "/tmp/g1.txt", NULL };
        caso("-m1 -c", a, NULL, 0, "1\n");
    }
    {
        char *a[] = { "/bin/grep", "-A1", "caju", "/tmp/g1.txt", NULL };
        caso("-A1", a, NULL, 0, "caju\nbanana split\n");
    }
    {
        char *a[] = { "/bin/grep", "-B1", "caju", "/tmp/g1.txt", NULL };
        caso("-B1", a, NULL, 0, "Abacaxi\ncaju\n");
    }
    {
        char *a[] = { "/bin/grep", "-C1", "caju", "/tmp/g1.txt", NULL };
        caso("-C1", a, NULL, 0, "Abacaxi\ncaju\nbanana split\n");
    }
    {
        char *a[] = { "/bin/grep", "-n", "-C1", "caju", "/tmp/g1.txt", NULL };
        caso("-n -C1: prefixos do contexto", a, NULL, 0,
             "2-Abacaxi\n3:caju\n4-banana split\n");
    }
    {
        char *a[] = { "/bin/grep", "-n", "-C0", "m", "/tmp/g6.txt", NULL };
        caso("-C0: separador de grupos", a, NULL, 0, "2:m\n--\n5:m\n");
    }
    {
        char *a[] = { "/bin/grep", "-n", "-C0", "--no-group-separator", "m",
                      "/tmp/g6.txt", NULL };
        caso("--no-group-separator", a, NULL, 0, "2:m\n5:m\n");
    }

    /* Nome do arquivo. */
    {
        char *a[] = { "/bin/grep", "-H", "banana", "/tmp/g1.txt", NULL };
        caso("-H: nome sempre", a, NULL, 0,
             "/tmp/g1.txt:banana\n/tmp/g1.txt:banana split\n");
    }
    {
        char *a[] = { "/bin/grep", "banana", "/tmp/g1.txt", "/tmp/g2.txt", NULL };
        caso("vários arquivos mostram o nome", a, NULL, 0,
             "/tmp/g1.txt:banana\n/tmp/g1.txt:banana split\n/tmp/g2.txt:banana\n");
    }
    {
        char *a[] = { "/bin/grep", "-h", "banana", "/tmp/g1.txt", "/tmp/g2.txt", NULL };
        caso("-h: sem nome", a, NULL, 0, "banana\nbanana split\nbanana\n");
    }
    {
        char *a[] = { "/bin/grep", "-l", "-Z", "banana", "/tmp/g1.txt", "/tmp/g5.txt", NULL };
        char buf[128];
        int rc = -1;
        int n = capturar(a, NULL, buf, sizeof buf, &rc);
        if (n == 12 && rc == 0 && memcmp(buf, "/tmp/g1.txt", 12) == 0) {
            imprimir("  [ok] -Z: NUL depois do nome\n");
        } else {
            imprimir("  [FALHA] -Z (codigo %d, %d bytes)\n", rc, n);
            falhas_teste++;
        }
    }

    /* Entrada padrão. */
    {
        char *a[] = { "/bin/grep", "banana", NULL };
        caso("entrada padrão", a, "banana\nnada\n", 0, "banana\n");
    }
    {
        char *a[] = { "/bin/grep", "-H", "banana", "-", NULL };
        caso("operando - é a entrada padrão", a, "banana\nnada\n", 0,
             "(standard input):banana\n");
    }

    /* Recursão e filtros. */
    {
        char *a[] = { "/bin/grep", "-r", "banana", "/tmp/sub", NULL };
        caso("-r: recursivo", a, NULL, 0,
             "/tmp/sub/skip/y.txt:banana\n/tmp/sub/x.txt:banana\n");
    }
    {
        char *a[] = { "/bin/grep", "-r", "--include=*.txt", "banana", "/tmp/sub", NULL };
        caso("--include", a, NULL, 0,
             "/tmp/sub/skip/y.txt:banana\n/tmp/sub/x.txt:banana\n");
    }
    {
        char *a[] = { "/bin/grep", "-r", "--exclude-dir=skip", "banana", "/tmp/sub", NULL };
        caso("--exclude-dir", a, NULL, 0, "/tmp/sub/x.txt:banana\n");
    }
    {
        char *a[] = { "/bin/grep", "-r", "--exclude=*.txt", "banana", "/tmp/sub", NULL };
        caso("--exclude", a, NULL, 1, "");
    }
    {
        char *a[] = { "/bin/grep", "-d", "skip", "banana", "/tmp/sub", NULL };
        caso("-d skip", a, NULL, 1, "");
    }

    /* Arquivos binários. */
    {
        char *a[] = { "/bin/grep", "a", "/tmp/bin.dat", NULL };
        caso("binário: mensagem", a, NULL, 0, "Binary file /tmp/bin.dat matches\n");
    }
    {
        char *a[] = { "/bin/grep", "-I", "a", "/tmp/bin.dat", NULL };
        caso("-I: pula binário", a, NULL, 1, "");
    }

    /* Cores. */
    {
        char *a[] = { "/bin/grep", "--color=always", "-n", "banana", "/tmp/g1.txt", NULL };
        contem_caso("--color=always pinta o trecho", a, NULL, 0,
                    "\x1b[01;31m\x1b[Kbanana\x1b[m\x1b[K");
    }
    {
        char *a[] = { "/bin/grep", "--color=never", "banana", "/tmp/g1.txt", NULL };
        caso("--color=never sem ESC", a, NULL, 0, "banana\nbanana split\n");
    }

    /* Erros. */
    {
        char *a[] = { "/bin/grep", "banana", "/tmp/nao_existe.txt", NULL };
        contem_caso("arquivo que falta: erro 2", a, NULL, 2, "grep:");
    }
    {
        char *a[] = { "/bin/grep", "-s", "banana", "/tmp/nao_existe.txt", NULL };
        caso("-s: erro sem mensagem", a, NULL, 2, "");
    }
    {
        char *a[] = { "/bin/grep", "-X", "banana", "/tmp/g1.txt", NULL };
        contem_caso("opção inválida: código 2", a, NULL, 2, "grep:");
    }
    {
        char *a[] = { "/bin/grep", "-V", NULL };
        contem_caso("-V: versão", a, NULL, 0, "grep");
    }

    apagar("/tmp/g1.txt"); apagar("/tmp/g2.txt"); apagar("/tmp/g3.txt");
    apagar("/tmp/g4.txt"); apagar("/tmp/g5.txt"); apagar("/tmp/g6.txt");
    apagar("/tmp/g7.txt"); apagar("/tmp/pat.txt"); apagar("/tmp/bin.dat");
    apagar("/tmp/sub/x.txt"); apagar("/tmp/sub/skip/y.txt");
    remover_pasta("/tmp/sub/skip"); remover_pasta("/tmp/sub");

    return resultado("h50_grep");
}
