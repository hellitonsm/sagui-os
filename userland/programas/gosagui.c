/* gosagui - primeiro compilador nativo do subconjunto Go do SaguiOS.
 * Sem runtime externo: emite ELF32 e chamadas da ABI de abi.h diretamente.
 * Deliberadamente NAO se apresenta como a toolchain oficial do Go. */
#include "sagui.h"

#define LIMITE 16384
#define BASE 0x40000000u
#define CAB 4096u
static char fonte[LIMITE + 1], texto[LIMITE + 1], token[128];
static unsigned char codigo[LIMITE], dados[LIMITE], imagem[CAB + 2 * LIMITE];
static unsigned pos, linha, nc, nd, nt;
static int tipo, erro;
enum { FIM = 256, NOME, TEXTO, SEP };

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
static void compilar(void)
{
    pos = nc = nd = 0; linha = 1; erro = 0;
    proximo(); separadores(); palavra("package"); palavra("main");
    exigir(SEP); separadores(); palavra("func"); palavra("main");
    exigir('('); exigir(')'); exigir('{'); separadores();
    while (!erro && tipo != '}' && tipo != FIM) {
        int nova_linha = !compara_texto(token, "println");
        if (tipo != NOME || (!nova_linha && compara_texto(token, "print"))) {
            falhar("somente print e println com um literal string sao suportados"); break;
        }
        proximo(); exigir('(');
        if (tipo != TEXTO) { falhar("esperado literal string"); break; }
        unsigned tamanho = nt + (unsigned)nova_linha;
        if (nd + tamanho > LIMITE) { falhar("dados excedem 16 KiB"); break; }
        memcpy(dados + nd, texto, nt);
        if (nova_linha) dados[nd + nt] = '\n';
        /* Segmento de dados fica depois dos 16 KiB reservados ao codigo. */
        mov(0, SIS_ESCREVER); mov(3, SAIDA);
        mov(1, BASE + LIMITE + nd); mov(2, tamanho); byte(0xcd); byte(0x80);
        /* Este pequeno runtime considera escrita curta um erro, nao sucesso. */
        byte(0x3d); inteiro(tamanho); /* cmp eax, tamanho */
        byte(0x74); byte(12);        /* je proxima instrucao Go */
        mov(0, SIS_SAIR); mov(3, 1); byte(0xcd); byte(0x80);
        nd += tamanho;
        proximo(); exigir(')');
        if (tipo != '}' && tipo != SEP) falhar("esperado fim de instrucao");
        separadores();
    }
    exigir('}'); separadores(); exigir(FIM);
    mov(0, SIS_SAIR); mov(3, 0); byte(0xcd); byte(0x80);
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
        imprimir("gosagui 0.1: subconjunto Go nativo, nao e a toolchain oficial\n"); return 0;
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
