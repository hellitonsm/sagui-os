/* gosagui - compilador nativo do subconjunto Go do SaguiOS.
 * Gera ELF32 com um bootstrap proprio (init -> main -> saida) e resolve
 * importacoes de bibliotecas embutidas para a ABI do SaguiOS.
 * Deliberadamente NAO se apresenta como a toolchain oficial do Go. */
#include "sagui.h"

#define LIMITE 16384
#define BASE 0x40000000u
#define CAB 4096u
#define MAX_IMPORT 16
#define MAX_FUNCAO 16
#define MAX_ARGS 16

static char fonte[LIMITE + 1], texto[LIMITE + 1], token[128];
static unsigned char codigo[LIMITE], dados[LIMITE], imagem[CAB + 2 * LIMITE];
static unsigned pos, linha, nc, nd, nt;
static int tipo, erro;
enum { FIM = 256, NOME, TEXTO, SEP };

typedef struct {
    char apelido[32];
    char caminho[32];
    int  usado;
} importacao_t;
static importacao_t imports[MAX_IMPORT];
static int n_imports;

static unsigned inits[MAX_FUNCAO];
static int n_inits, tem_main;
static unsigned pos_main;

typedef struct { unsigned at, n; } arg_t;
static arg_t args[MAX_ARGS];
static int n_args;
static unsigned off_nl, off_esp;

static void falhar(const char *mensagem)
{
    if (!erro) imprimir_em(ERROS, "gosagui:%u: %s\n", linha, mensagem);
    erro = 1;
}
static int letra(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static void proximo(void)
{
    unsigned n = 0;
    token[0] = 0;
    for (;;) {
        int c = fonte[pos];
        if (c == ' ' || c == '\t' || c == '\r') { pos++; continue; }
        if (c == '/' && fonte[pos+1] == '/') {
            while (fonte[pos] && fonte[pos] != '\n') pos++;
            continue;
        }
        if (c == '/' && fonte[pos+1] == '*') {
            int quebrou = 0;
            pos += 2;
            while (fonte[pos] && !(fonte[pos] == '*' && fonte[pos+1] == '/')) {
                if (fonte[pos++] == '\n') { linha++; quebrou = 1; }
            }
            if (!fonte[pos]) { falhar("comentario nao terminado"); tipo = FIM; return; }
            pos += 2;
            if (quebrou) { tipo = SEP; return; }
            continue;
        }
        break;
    }
    int c = fonte[pos];
    if (!c) { tipo = FIM; return; }
    pos++;
    if (c == '\n' || c == ';') { if (c == '\n') linha++; tipo = SEP; return; }
    if (letra(c)) {
        token[n++] = (char)c;
        while (letra(fonte[pos]) || (fonte[pos] >= '0' && fonte[pos] <= '9')) {
            if (n >= sizeof token - 1) { falhar("identificador muito longo"); tipo = FIM; return; }
            token[n++] = fonte[pos++];
        }
        token[n] = 0; tipo = NOME; return;
    }
    if (c == '"') {
        nt = 0;
        while (fonte[pos] && fonte[pos] != '"') {
            c = (unsigned char)fonte[pos++];
            if (c == '\n' || c == '\r') { falhar("quebra de linha em string"); break; }
            if (c == '\\') {
                if (!fonte[pos]) { falhar("escape nao terminado"); break; }
                c = fonte[pos++];
                if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
                else if (c == 'r') c = '\r';
                else if (c != '\\' && c != '"') { falhar("escape nao suportado"); break; }
            }
            texto[nt++] = (char)c;
        }
        if (fonte[pos] != '"') falhar("string nao terminada");
        else pos++;
        tipo = TEXTO; return;
    }
    tipo = c;
}
static void separadores(void) { while (!erro && tipo == SEP) proximo(); }
static void exigir(int t)
{
    if (erro) return;
    if (tipo != t) { falhar("sintaxe nao suportada ou token inesperado"); return; }
    proximo();
}
static void palavra(const char *s)
{
    if (tipo != NOME || compara_texto(token, s)) { falhar(s); return; }
    proximo();
}
static void byte(unsigned v)
{
    if (nc == LIMITE) { falhar("codigo excede 16 KiB"); return; }
    codigo[nc++] = (unsigned char)v;
}
static void inteiro(unsigned v) { for (unsigned i = 0; i < 4; i++) byte(v >> (8*i)); }
static void mov(unsigned reg, unsigned v) { byte(0xb8 + reg); inteiro(v); }
static void guardar(unsigned off, unsigned v, unsigned n)
{
    for (unsigned i = 0; i < n; i++) imagem[off+i] = (unsigned char)(v >> (8*i));
}
static void guardar_codigo(unsigned off, unsigned v, unsigned n)
{
    for (unsigned i = 0; i < n; i++) codigo[off+i] = (unsigned char)(v >> (8*i));
}
static void chamar(unsigned alvo)
{
    byte(0xe8);
    unsigned off = nc;
    inteiro(0);
    guardar_codigo(off, alvo - (off + 4), 4);
}
static void apelido_de(const char *caminho, char *saida, unsigned max)
{
    const char *barra = acha_ultimo_char(caminho, '/');
    copia_texto(saida, barra ? barra + 1 : caminho, max);
}
static int pacote_ok(const char *caminho)
{
    return !compara_texto(caminho, "fmt") || !compara_texto(caminho, "sagui");
}
static void espec_import(void)
{
    char apelido[32];
    if (tipo == NOME) {
        copia_texto(apelido, token, sizeof apelido);
        proximo();
        if (tipo != TEXTO) { falhar("esperado caminho de importacao"); return; }
    } else if (tipo == TEXTO) {
        apelido_de(texto, apelido, sizeof apelido);
    } else {
        falhar("esperado caminho de importacao");
        return;
    }
    if (n_imports == MAX_IMPORT) { falhar("muitas importacoes"); return; }
    if (!pacote_ok(texto)) { falhar("pacote importado nao suportado"); return; }
    for (int i = 0; i < n_imports; i++)
        if (!compara_texto(imports[i].apelido, apelido)) { falhar("apelido de importacao repetido"); return; }
    copia_texto(imports[n_imports].apelido, apelido, sizeof imports[0].apelido);
    copia_texto(imports[n_imports].caminho, texto, sizeof imports[0].caminho);
    imports[n_imports].usado = 0;
    n_imports++;
    proximo();
}
static void ler_imports(void)
{
    while (!erro && tipo == NOME && !compara_texto(token, "import")) {
        proximo();
        if (tipo == '(') {
            proximo();
            separadores();
            while (!erro && tipo != ')' && tipo != FIM) {
                espec_import();
                if (erro) return;
                if (tipo != ')' && tipo != SEP) { falhar("esperado fim de linha na importacao"); return; }
                separadores();
            }
            exigir(')');
        } else {
            espec_import();
        }
        if (erro) return;
        if (tipo != SEP && tipo != FIM) { falhar("esperado fim de linha apos importacao"); return; }
        separadores();
    }
}
static void ler_args_texto(int terminar)
{
    n_args = 0;
    separadores();
    if (tipo == ')') return;
    for (;;) {
        if (tipo != TEXTO) { falhar("esperado literal string"); return; }
        if (n_args == MAX_ARGS) { falhar("muitos argumentos"); return; }
        unsigned extra = terminar ? 1 : 0;
        if (nd + nt + extra > LIMITE) { falhar("dados excedem 16 KiB"); return; }
        args[n_args].at = nd;
        args[n_args].n = nt;
        memcpy(dados + nd, texto, nt);
        nd += nt;
        if (terminar) dados[nd++] = 0;
        n_args++;
        proximo();
        separadores();
        if (tipo == ',') { proximo(); separadores(); continue; }
        break;
    }
}
enum { ALVO_NENHUM, ALVO_FMT_PRINT, ALVO_FMT_PRINTLN,
       ALVO_SAGUI_ERRO, ALVO_SAGUI_REGISTRAR, ALVO_SAGUI_LIMPAR };
static int resolver(const char *apelido, const char *funcao)
{
    int i = -1;
    for (int k = 0; k < n_imports; k++)
        if (!compara_texto(imports[k].apelido, apelido)) { i = k; break; }
    if (i < 0) { falhar("pacote nao importado"); return ALVO_NENHUM; }
    imports[i].usado = 1;
    if (!compara_texto(imports[i].caminho, "fmt")) {
        if (!compara_texto(funcao, "Print")) return ALVO_FMT_PRINT;
        if (!compara_texto(funcao, "Println")) return ALVO_FMT_PRINTLN;
        falhar("funcao nao suportada no pacote fmt");
        return ALVO_NENHUM;
    }
    if (!compara_texto(imports[i].caminho, "sagui")) {
        if (!compara_texto(funcao, "Erro")) return ALVO_SAGUI_ERRO;
        if (!compara_texto(funcao, "Registrar")) return ALVO_SAGUI_REGISTRAR;
        if (!compara_texto(funcao, "LimparTela")) return ALVO_SAGUI_LIMPAR;
        falhar("funcao nao suportada no pacote sagui");
        return ALVO_NENHUM;
    }
    falhar("pacote nao suportado");
    return ALVO_NENHUM;
}
static void escrever_dados(unsigned fd, unsigned off, unsigned tam)
{
    if (!tam || erro) return;
    mov(0, SIS_ESCREVER); mov(3, fd);
    mov(1, BASE + LIMITE + off); mov(2, tam); byte(0xcd); byte(0x80);
    /* Escrita curta encerra com 1, como no runtime anterior. */
    byte(0x3d); inteiro(tam);
    byte(0x74); byte(12);
    mov(0, SIS_SAIR); mov(3, 1); byte(0xcd); byte(0x80);
}
static void registrar_dados(unsigned off)
{
    mov(0, SIS_REGISTRAR);
    mov(3, BASE + LIMITE + off);
    mov(1, 0);
    mov(2, 0);
    byte(0xcd); byte(0x80);
}
static void emitir_limpar_tela(void)
{
    mov(0, SIS_LIMPAR_TELA);
    byte(0xcd); byte(0x80);
}
static void emitir_chamada(int alvo)
{
    switch (alvo) {
    case ALVO_FMT_PRINT:
        for (int i = 0; i < n_args; i++) escrever_dados(SAIDA, args[i].at, args[i].n);
        break;
    case ALVO_FMT_PRINTLN:
        for (int i = 0; i < n_args; i++) {
            if (i) escrever_dados(SAIDA, off_esp, 1);
            escrever_dados(SAIDA, args[i].at, args[i].n);
        }
        escrever_dados(SAIDA, off_nl, 1);
        break;
    case ALVO_SAGUI_ERRO:
        for (int i = 0; i < n_args; i++) escrever_dados(ERROS, args[i].at, args[i].n);
        break;
    case ALVO_SAGUI_REGISTRAR:
        for (int i = 0; i < n_args; i++) registrar_dados(args[i].at);
        break;
    case ALVO_SAGUI_LIMPAR:
        if (n_args) { falhar("LimparTela nao recebe argumentos"); break; }
        emitir_limpar_tela();
        break;
    default:
        break;
    }
}
static void instrucao(void)
{
    if (tipo != NOME) { falhar("instrucao nao suportada"); return; }
    char nome[128];
    copia_texto(nome, token, sizeof nome);
    proximo();
    int alvo = ALVO_NENHUM;
    int terminar = 0;
    if (tipo == '.') {
        proximo();
        if (tipo != NOME) { falhar("esperado nome de funcao"); return; }
        alvo = resolver(nome, token);
        if (erro) return;
        if (alvo == ALVO_SAGUI_REGISTRAR) terminar = 1;
        proximo();
    } else if (!compara_texto(nome, "print")) {
        alvo = ALVO_FMT_PRINT;
    } else if (!compara_texto(nome, "println")) {
        alvo = ALVO_FMT_PRINTLN;
    } else {
        falhar("funcao fora de pacote importado");
        return;
    }
    exigir('(');
    if (erro) return;
    ler_args_texto(terminar);
    if (erro) return;
    exigir(')');
    if (erro) return;
    emitir_chamada(alvo);
}
static void ler_funcoes(void)
{
    while (!erro && tipo == NOME && !compara_texto(token, "func")) {
        proximo();
        if (tipo != NOME) { falhar("esperado nome de funcao"); return; }
        int eh_init = !compara_texto(token, "init");
        int eh_main = !compara_texto(token, "main");
        if (!eh_init && !eh_main) { falhar("somente func main e func init sao suportadas"); return; }
        proximo();
        exigir('('); exigir(')'); exigir('{');
        if (erro) return;
        separadores();
        unsigned inicio = nc;
        while (!erro && tipo != '}' && tipo != FIM) {
            instrucao();
            if (erro) return;
            if (tipo != '}' && tipo != SEP) { falhar("esperado fim de instrucao"); return; }
            separadores();
        }
        exigir('}');
        if (erro) return;
        byte(0xc3);
        if (eh_main) {
            if (tem_main) { falhar("mais de um func main"); return; }
            tem_main = 1;
            pos_main = inicio;
        } else {
            if (n_inits == MAX_FUNCAO) { falhar("muitas funcs init"); return; }
            inits[n_inits++] = inicio;
        }
        separadores();
    }
}
static void compilar(void)
{
    pos = nc = nd = 0; linha = 1; erro = 0;
    n_imports = n_inits = 0; tem_main = 0; pos_main = 0;
    /* Separadores usados por Println ficam antes dos literais do programa. */
    off_nl = nd; dados[nd++] = '\n';
    off_esp = nd; dados[nd++] = ' ';
    /* Bootstrap: o ponto de entrada salta para o bloco init/main/saida. */
    byte(0xe9);
    unsigned salto = nc;
    inteiro(0);
    proximo(); separadores(); palavra("package"); palavra("main");
    exigir(SEP); separadores();
    ler_imports();
    if (erro) return;
    ler_funcoes();
    if (erro) return;
    if (!tem_main) { falhar("func main ausente"); return; }
    for (int i = 0; i < n_imports; i++)
        if (!imports[i].usado) { falhar("importacao nao usada"); return; }
    separadores();
    exigir(FIM);
    if (erro) return;
    unsigned boot = nc;
    for (int i = 0; i < n_inits; i++) chamar(inits[i]);
    chamar(pos_main);
    mov(0, SIS_SAIR);
    byte(0x31); byte(0xdb);          /* xor ebx, ebx */
    byte(0xcd); byte(0x80);
    guardar_codigo(salto, boot - (salto + 4), 4);
}
static unsigned elf(void)
{
    unsigned total = CAB + LIMITE + nd;
    memset(imagem, 0, total);
    imagem[0] = 0x7f; imagem[1] = 'E'; imagem[2] = 'L'; imagem[3] = 'F';
    imagem[4] = imagem[5] = imagem[6] = 1;
    guardar(16, 2, 2); guardar(18, 3, 2); guardar(20, 1, 4);
    guardar(24, BASE, 4); guardar(28, 52, 4);
    guardar(40, 52, 2); guardar(42, 32, 2); guardar(44, 1, 2);
    guardar(52, 1, 4); guardar(56, CAB, 4); guardar(60, BASE, 4);
    guardar(64, BASE, 4); guardar(68, LIMITE + nd, 4);
    guardar(72, LIMITE + nd, 4); guardar(76, 5, 4); guardar(80, 4096, 4);
    memcpy(imagem + CAB, codigo, nc); memcpy(imagem + CAB + LIMITE, dados, nd);
    return total;
}
int principal(int argc, char **argv)
{
    if (argc == 2 && !compara_texto(argv[1], "version")) {
        imprimir("gosagui 0.2: subconjunto Go nativo com bootstrap e importacoes\n");
        return 0;
    }
    if (argc != 5 || compara_texto(argv[1], "build") || compara_texto(argv[2], "-o")) {
        imprimir_em(ERROS, "uso: gosagui build -o destino fonte.go\n"); return 1;
    }
    if (!compara_texto(argv[3], argv[4])) {
        imprimir_em(ERROS, "gosagui: origem e destino iguais\n"); return 1;
    }
    int fd = abrir(argv[4], ABRIR_LER);
    if (fd < 0) { imprimir_em(ERROS, "gosagui: fonte: %s\n", nome_erro(fd)); return 1; }
    unsigned n = 0;
    int r = 0;
    while (n < sizeof fonte - 1 && (r = ler(fd, fonte + n, sizeof fonte - 1 - n)) > 0) n += (unsigned)r;
    char extra;
    int excedeu = n == LIMITE ? ler(fd, &extra, 1) : 0;
    fechar(fd);
    if (r < 0 || excedeu != 0) {
        imprimir_em(ERROS, "gosagui: erro de leitura ou fonte maior que 16 KiB\n"); return 1;
    }
    for (unsigned i = 0; i < n; i++) if (!fonte[i]) {
        imprimir_em(ERROS, "gosagui: byte NUL na fonte\n"); return 1;
    }
    fonte[n] = 0;
    compilar();
    if (erro) return 1; /* Nunca trunca o destino se a analise falhar. */
    unsigned tamanho = elf();
    fd = abrir(argv[3], ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd < 0) { imprimir_em(ERROS, "gosagui: destino: %s\n", nome_erro(fd)); return 1; }
    r = escrever_tudo(fd, imagem, tamanho);
    int f = fechar(fd);
    if (r < 0 || f < 0) { apagar(argv[3]); imprimir_em(ERROS, "gosagui: erro de gravacao\n"); return 1; }
    imprimir("gosagui: %s gerado (%u bytes)\n", argv[3], tamanho);
    return 0;
}
