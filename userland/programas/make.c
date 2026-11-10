/* make.c - o "make" do SaguiOS: compila programas descritos em arquivos .mk.
 *
 * Um arquivo .mk guarda tudo o que o tinycc precisa para compilar um
 * programa: o fonte (ou os fontes), o nome do executavel, as bibliotecas,
 * as opcoes e o compilador. O make so le, monta a linha de comando e roda.
 *
 *   make /jogos/grafico/sudoku.mk   compila um programa
 *   make -all /jogos/grafico        compila todos os .mk da pasta
 *   make                            lista os .mk da pasta atual
 *
 * Formato do .mk (uma chave por linha, # comenta):
 *   NOME        = Sudoku grafico
 *   FONTE       = sudoku.c
 *   SAIDA       = sudoku
 *   BIBLIOTECAS = -lsaguigui
 *   OPCOES      = -static
 *   INCLUI      = -I/usr/include
 *   COMPILADOR  = /bin/tinycc
 *   EXECUTAR    = nao
 *
 * Caminhos relativos do fonte sao resolvidos a partir da pasta do .mk; a
 * SAIDA fica na pasta atual (como no gcc: `gcc fonte.c` gera ./fonte). */
#include "sagui.h"

#define MAX_RECEITA 8192
#define MAX_FONTES  16
#define MAX_TOKENS  32

typedef struct {
    char nome[80];
    char fonte[512];
    char fontes[512];
    char saida[256];
    char bibliotecas[256];
    char opcoes[256];
    char inclui[256];
    char compilador[128];
    char executar[16];
} receita_t;

/* ------------------------------------------------------------------ texto */

static void aparar(char *s)
{
    char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    if (p != s) memmove(s, p, tam_texto(p) + 1);
    size_t n = tam_texto(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
        s[--n] = 0;
}

static void tirar_aspas(char *s)
{
    size_t n = tam_texto(s);
    if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') ||
                   (s[0] == '\'' && s[n - 1] == '\''))) {
        memmove(s, s + 1, n - 1);
        s[n - 2] = 0;
    }
}

/* Junta "pasta/nome"; pasta "." ou vazia devolve so o nome. */
static void juntar(char *dst, size_t max, const char *pasta, const char *nome)
{
    if (!pasta || !*pasta || compara_texto(pasta, ".") == 0) {
        copia_texto(dst, nome, max);
        return;
    }
    size_t n = tam_texto(pasta);
    if (pasta[0] == '/' && n == 1)
        formatar_texto(dst, max, "/%s", nome);
    else if (pasta[n - 1] == '/')
        formatar_texto(dst, max, "%s%s", pasta, nome);
    else
        formatar_texto(dst, max, "%s/%s", pasta, nome);
}

static void pasta_de(const char *caminho, char *dst, size_t max)
{
    const char *barra = acha_ultimo_char(caminho, '/');
    if (!barra) {
        copia_texto(dst, ".", max);
        return;
    }
    if (barra == caminho) {
        copia_texto(dst, "/", max);
        return;
    }
    size_t n = (size_t)(barra - caminho);
    if (n >= max) n = max - 1;
    memcpy(dst, caminho, n);
    dst[n] = 0;
}

static const char *nome_de(const char *caminho)
{
    const char *barra = acha_ultimo_char(caminho, '/');
    return barra ? barra + 1 : caminho;
}

static void sem_extensao(const char *nome, char *dst, size_t max)
{
    copia_texto(dst, nome, max);
    char *p = acha_ultimo_char(dst, '.');
    if (p && p != dst) *p = 0;
}

static bool termina_com(const char *s, const char *sufixo)
{
    size_t a = tam_texto(s), b = tam_texto(sufixo);
    return a >= b && compara_texto(s + a - b, sufixo) == 0;
}

static int dividir(char *s, char *tok[], int max)
{
    int n = 0;
    char *p = s;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        tok[n++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    return n;
}

/* ------------------------------------------------------------------ .mk */

static int ler_receita(const char *caminho, receita_t *r)
{
    memset(r, 0, sizeof *r);
    copia_texto(r->compilador, "/bin/tinycc", sizeof r->compilador);
    copia_texto(r->bibliotecas, "-lsaguigui", sizeof r->bibliotecas);
    copia_texto(r->executar, "nao", sizeof r->executar);

    static char buf[MAX_RECEITA];
    int fd = abrir(caminho, ABRIR_LER);
    if (fd < 0) return fd;
    int n = ler(fd, buf, sizeof buf - 1);
    fechar(fd);
    if (n < 0) return n;
    buf[n] = 0;

    char *p = buf;
    while (*p) {
        char *fim = acha_char(p, '\n');
        if (fim) *fim = 0;
        char *linha = p;
        aparar(linha);
        if (*linha && *linha != '#') {
            char *igual = acha_char(linha, '=');
            if (igual) {
                *igual = 0;
                char *chave = linha;
                char *valor = igual + 1;
                aparar(chave);
                aparar(valor);
                tirar_aspas(valor);

                if (compara_texto(chave, "NOME") == 0)
                    copia_texto(r->nome, valor, sizeof r->nome);
                else if (compara_texto(chave, "FONTE") == 0 ||
                         compara_texto(chave, "SRC") == 0)
                    copia_texto(r->fonte, valor, sizeof r->fonte);
                else if (compara_texto(chave, "FONTES") == 0 ||
                         compara_texto(chave, "SOURCES") == 0)
                    copia_texto(r->fontes, valor, sizeof r->fontes);
                else if (compara_texto(chave, "SAIDA") == 0 ||
                         compara_texto(chave, "ALVO") == 0 ||
                         compara_texto(chave, "TARGET") == 0 ||
                         compara_texto(chave, "OUT") == 0)
                    copia_texto(r->saida, valor, sizeof r->saida);
                else if (compara_texto(chave, "BIBLIOTECAS") == 0 ||
                         compara_texto(chave, "LIBS") == 0)
                    copia_texto(r->bibliotecas, valor, sizeof r->bibliotecas);
                else if (compara_texto(chave, "OPCOES") == 0 ||
                         compara_texto(chave, "CFLAGS") == 0 ||
                         compara_texto(chave, "FLAGS") == 0)
                    copia_texto(r->opcoes, valor, sizeof r->opcoes);
                else if (compara_texto(chave, "INCLUI") == 0 ||
                         compara_texto(chave, "INCLUDES") == 0)
                    copia_texto(r->inclui, valor, sizeof r->inclui);
                else if (compara_texto(chave, "COMPILADOR") == 0 ||
                         compara_texto(chave, "CC") == 0)
                    copia_texto(r->compilador, valor, sizeof r->compilador);
                else if (compara_texto(chave, "EXECUTAR") == 0 ||
                         compara_texto(chave, "RUN") == 0)
                    copia_texto(r->executar, valor, sizeof r->executar);
            }
        }
        if (!fim) break;
        p = fim + 1;
    }
    return 0;
}

static bool quer_executar(const receita_t *r)
{
    return compara_texto(r->executar, "sim") == 0 ||
           compara_texto(r->executar, "1") == 0 ||
           compara_texto(r->executar, "yes") == 0 ||
           compara_texto(r->executar, "true") == 0;
}

/* ------------------------------------------------------------------ compilar */

static int compilar(const char *caminho_mk, bool pode_executar)
{
    receita_t r;
    int erro = ler_receita(caminho_mk, &r);
    if (erro < 0) {
        imprimir_em(ERROS, "make: %s: %s\n", caminho_mk, nome_erro(erro));
        return 1;
    }

    char pasta[256];
    pasta_de(caminho_mk, pasta, sizeof pasta);

    static char copia_fonte[512], copia_fontes[512];
    copia_texto(copia_fonte, r.fonte, sizeof copia_fonte);
    copia_texto(copia_fontes, r.fontes, sizeof copia_fontes);

    char *brutos[MAX_FONTES];
    int nbrutos = 0;
    char *tok[MAX_TOKENS];
    int n = dividir(copia_fonte, tok, MAX_TOKENS);
    for (int i = 0; i < n && nbrutos < MAX_FONTES; i++) brutos[nbrutos++] = tok[i];
    n = dividir(copia_fontes, tok, MAX_TOKENS);
    for (int i = 0; i < n && nbrutos < MAX_FONTES; i++) brutos[nbrutos++] = tok[i];

    if (nbrutos == 0) {
        imprimir_em(ERROS, "make: %s: falta FONTE (ou FONTES)\n", caminho_mk);
        return 1;
    }

    char fontes[MAX_FONTES][512];
    for (int i = 0; i < nbrutos; i++)
        juntar(fontes[i], sizeof fontes[i], pasta, brutos[i]);

    char saida[256];
    if (r.saida[0])
        copia_texto(saida, r.saida, sizeof saida);
    else
        sem_extensao(nome_de(brutos[0]), saida, sizeof saida);

    char *argv[128];
    int argc = 0;
    char compilador[160];
    if (acha_char(r.compilador, '/'))
        copia_texto(compilador, r.compilador, sizeof compilador);
    else
        formatar_texto(compilador, sizeof compilador, "/bin/%s", r.compilador);
    argv[argc++] = compilador;

    n = dividir(r.opcoes, tok, MAX_TOKENS);
    for (int i = 0; i < n && argc < 120; i++) argv[argc++] = tok[i];

    n = dividir(r.inclui, tok, MAX_TOKENS);
    for (int i = 0; i < n && argc < 120; i++) argv[argc++] = tok[i];

    for (int i = 0; i < nbrutos && argc < 120; i++) argv[argc++] = fontes[i];

    argv[argc++] = "-o";
    argv[argc++] = saida;

    n = dividir(r.bibliotecas, tok, MAX_TOKENS);
    for (int i = 0; i < n && argc < 125; i++) argv[argc++] = tok[i];

    argv[argc] = NULL;

    if (r.nome[0]) imprimir("[make] %s\n", r.nome);
    else imprimir("[make] %s\n", caminho_mk);
    imprimir("[make] %s -> %s\n", fontes[0], saida);

    int pid = executar(argv[0], argv);
    if (pid < 0) {
        imprimir_em(ERROS, "make: %s: %s\n", argv[0], nome_erro(pid));
        return 1;
    }
    int codigo = 0;
    if (esperar(pid, &codigo) != pid) {
        imprimir_em(ERROS, "make: nao consegui esperar o compilador\n");
        return 1;
    }
    if (codigo != 0) {
        imprimir_em(ERROS, "[make] falhou (codigo %d): %s\n", codigo, caminho_mk);
        return 1;
    }
    imprimir("[make] ok: %s\n", saida);

    if (pode_executar && quer_executar(&r)) {
        char prog[320];
        if (acha_char(saida, '/')) copia_texto(prog, saida, sizeof prog);
        else formatar_texto(prog, sizeof prog, "./%s", saida);
        char *av[2];
        av[0] = prog;
        av[1] = NULL;
        imprimir("[make] executando %s\n", prog);
        int p = executar(prog, av);
        if (p < 0) {
            imprimir_em(ERROS, "make: %s: %s\n", prog, nome_erro(p));
            return 1;
        }
        esperar(p, &codigo);
        return codigo == 0 ? 0 : 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ -all */

static int compilar_todos(const char *pasta)
{
    char nomes[32][NOME_MAX];
    int qtd = 0;
    entrada_pasta_t e;
    for (int i = 0; listar(pasta, i, &e) == 1; i++) {
        if (e.tipo == TIPO_PASTA || !termina_com(e.nome, ".mk")) continue;
        if (qtd < 32) copia_texto(nomes[qtd++], e.nome, NOME_MAX);
    }

    /* Ordem alfabetica, para a saida ficar previsivel. */
    for (int i = 1; i < qtd; i++) {
        char tmp[NOME_MAX];
        copia_texto(tmp, nomes[i], NOME_MAX);
        int k = i - 1;
        while (k >= 0 && compara_texto(nomes[k], tmp) > 0) {
            copia_texto(nomes[k + 1], nomes[k], NOME_MAX);
            k--;
        }
        copia_texto(nomes[k + 1], tmp, NOME_MAX);
    }

    if (qtd == 0) {
        imprimir("make: nenhum arquivo .mk em %s\n", pasta);
        return 1;
    }

    imprimir("[make] %d arquivo(s) .mk em %s\n", qtd, pasta);
    int falhas = 0;
    for (int i = 0; i < qtd; i++) {
        char caminho[512];
        juntar(caminho, sizeof caminho, pasta, nomes[i]);
        if (compilar(caminho, false) != 0) falhas++;
    }
    if (falhas > 0) {
        imprimir_em(ERROS, "[make] %d de %d falharam\n", falhas, qtd);
        return 1;
    }
    imprimir("[make] todos os %d compilaram\n", qtd);
    return 0;
}

static int listar_mk(const char *pasta)
{
    entrada_pasta_t e;
    int achou = 0;
    for (int i = 0; listar(pasta, i, &e) == 1; i++) {
        if (e.tipo == TIPO_PASTA || !termina_com(e.nome, ".mk")) continue;
        imprimir("  %s\n", e.nome);
        achou = 1;
    }
    if (!achou) imprimir("make: nenhum arquivo .mk em %s\n", pasta);
    return achou;
}

static void uso(void)
{
    imprimir("make: compila programas do SaguiOS descritos em arquivos .mk\n\n");
    imprimir("Uso:\n");
    imprimir("  make <arquivo.mk>     compila o programa do arquivo\n");
    imprimir("  make -all [pasta]     compila todos os .mk da pasta (padrao: .)\n");
    imprimir("  make                  lista os .mk da pasta atual\n\n");
    imprimir("Campos do .mk:\n");
    imprimir("  NOME, FONTE (ou FONTES), SAIDA, BIBLIOTECAS, OPCOES,\n");
    imprimir("  INCLUI, COMPILADOR, EXECUTAR\n");
}

int principal(int argc, char **argv)
{
    if (argc < 2) {
        uso();
        imprimir("\nArquivos .mk na pasta atual:\n");
        listar_mk(".");
        return 0;
    }
    if (compara_texto(argv[1], "-all") == 0 || compara_texto(argv[1], "--all") == 0)
        return compilar_todos(argc > 2 ? argv[2] : ".");
    if (argv[1][0] == '-' && argv[1][1] != 0) {
        uso();
        return 1;
    }
    return compilar(argv[1], true);
}
