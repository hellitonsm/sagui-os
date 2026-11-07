/* csagui - compilador nativo de um subconjunto pequeno de C para o SaguiOS.
 * Gera assembly da sintaxe documentada e chama /bin/asagui para criar ELF32.
 * Nao e GCC/Clang e nao pretende implementar C completo. */
#include "sagui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#ifndef ASAGUI_PATH
#define ASAGUI_PATH "/bin/asagui"
#endif

#define FONTE_MAX 8192u
#define TOKENS_MAX 4096
#define NOS_MAX 4096
#define ARGUMENTOS_MAX 4096
#define FUNCOES_MAX 64
#define PARAMETROS_MAX 8
#define VARIAVEIS_MAX 32
#define STRING_MAX 8192u
#define STRINGS_MAX 128
#define ASM_MAX 16384u
enum { T_FIM = 256, T_ID, T_NUM, T_STRING, T_EQ, T_NE, T_LE, T_GE, T_AND, T_OR };
enum { N_NUM = 1, N_STRING, N_VARIAVEL, N_UNARIO, N_BINARIO, N_ATRIB, N_CHAMADA };
enum { K_MAIS = '+', K_MENOS = '-', K_VEZES = '*', K_DIV = '/', K_MOD = '%',
       K_MENOR = '<', K_MAIOR = '>', K_IGUAL = T_EQ, K_DIF = T_NE,
       K_MENOR_IGUAL = T_LE, K_MAIOR_IGUAL = T_GE,
       K_E_LOGICO = T_AND, K_OU_LOGICO = T_OR };

typedef struct {
    unsigned kind, off, len, linha, valor;
} token_t;
typedef struct {
    unsigned kind, op;
    int a, b;
    unsigned valor;
    int token_nome;
    int primeiro_arg, n_args;
    unsigned profundidade;
} no_t;
typedef struct { int expressao, proximo; } arg_no_t;
typedef struct {
    int nome_token;
    unsigned corpo_inicio, corpo_fim;
    int n_parametros;
    int parametros[PARAMETROS_MAX];
} funcao_t;
typedef struct { int nome_token, deslocamento; } variavel_t;
typedef struct { unsigned off, n, indice; } string_t;

static char fonte[FONTE_MAX + 1];
static char strings[STRING_MAX];
static char assembly[ASM_MAX];
static unsigned char strings_asm[STRING_MAX];
static token_t tokens[TOKENS_MAX];
static no_t nos[NOS_MAX];
static arg_no_t argumentos[ARGUMENTOS_MAX];
static funcao_t funcoes[FUNCOES_MAX];
static variavel_t variaveis[VARIAVEIS_MAX];
static string_t strings_emitidas[STRINGS_MAX];
static unsigned n_tokens, n_strings, n_nos, n_argumentos;
static unsigned n_funcoes, n_strings_emitidas, n_bytes_strings;
static unsigned n_assembly, proxima_etiqueta;
static unsigned cursor, profundidade_expr, profundidade_stmt;
static int funcao_atual, n_variaveis, erro;
static int modo_objeto;

static void falhar(unsigned linha, const char *mensagem)
{
    if (!erro) fprintf(stderr, "csagui:%u: %s\n", linha, mensagem);
    erro = 1;
}

static int letra(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int digito(int c) { return c >= '0' && c <= '9'; }
static int espaco(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

static int token_adicionar(unsigned kind, unsigned off, unsigned len, unsigned linha, unsigned valor)
{
    if (n_tokens == TOKENS_MAX) { falhar(linha, "limite de tokens atingido"); return 0; }
    tokens[n_tokens].kind = kind; tokens[n_tokens].off = off; tokens[n_tokens].len = len;
    tokens[n_tokens].linha = linha; tokens[n_tokens].valor = valor;
    n_tokens++;
    return 1;
}

static int numero_literal(unsigned inicio, unsigned fim, unsigned linha, unsigned *valor)
{
    unsigned base = 10, i = inicio;
    unsigned long long v = 0;
    if (fim - inicio >= 2 && fonte[i] == '0' && (fonte[i + 1] == 'x' || fonte[i + 1] == 'X')) {
        base = 16; i += 2;
        if (i == fim) { falhar(linha, "literal hexadecimal sem digitos"); return 0; }
    }
    for (; i < fim; i++) {
        int c = (unsigned char)fonte[i], d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else { falhar(linha, "sufixo em literal inteiro nao suportado"); return 0; }
        if ((unsigned)d >= base) { falhar(linha, "digito invalido em literal inteiro"); return 0; }
        v = v * base + (unsigned)d;
        if (v > 0xFFFFFFFFULL) { falhar(linha, "literal inteiro maior que 32 bits"); return 0; }
    }
    *valor = (unsigned)v;
    return 1;
}

static int lexar(void)
{
    unsigned p = 0, linha = 1;
    while (fonte[p] && !erro) {
        unsigned char c = (unsigned char)fonte[p];
        if (espaco(c)) { if (c == '\n') linha++; p++; continue; }
        if (c == '#') {
            /* Diretivas de pre-processador sao ignoradas; nao ha expansao de macros/includes. */
            while (fonte[p] && fonte[p] != '\n') p++;
            continue;
        }
        if (c == '/' && fonte[p + 1] == '/') {
            p += 2; while (fonte[p] && fonte[p] != '\n') p++; continue;
        }
        if (c == '/' && fonte[p + 1] == '*') {
            p += 2; int fechou = 0;
            while (fonte[p]) {
                if (fonte[p] == '\n') linha++;
                if (fonte[p] == '*' && fonte[p + 1] == '/') { p += 2; fechou = 1; break; }
                p++;
            }
            if (!fechou) falhar(linha, "comentario /* nao terminado");
            continue;
        }
        if (letra(c)) {
            unsigned inicio = p++;
            while (letra((unsigned char)fonte[p]) || digito((unsigned char)fonte[p])) p++;
            if (p - inicio > 63) { falhar(linha, "identificador excede 63 caracteres"); break; }
            token_adicionar(T_ID, inicio, p - inicio, linha, 0);
            continue;
        }
        if (digito(c)) {
            unsigned inicio = p++;
            if (c == '0' && (fonte[p] == 'x' || fonte[p] == 'X')) {
                p++; while (digito((unsigned char)fonte[p]) ||
                    (fonte[p] >= 'a' && fonte[p] <= 'f') || (fonte[p] >= 'A' && fonte[p] <= 'F')) p++;
            } else while (digito((unsigned char)fonte[p])) p++;
            unsigned v;
            if (!numero_literal(inicio, p, linha, &v)) break;
            token_adicionar(T_NUM, inicio, p - inicio, linha, v);
            continue;
        }
        if (c == '"') {
            p++;
            unsigned off = n_strings, n = 0;
            int fechou = 0;
            while (fonte[p]) {
                unsigned char ch = (unsigned char)fonte[p++];
                if (ch == '"') { fechou = 1; break; }
                if (ch == '\n' || ch == '\r') { falhar(linha, "string atravessa linha"); break; }
                if (ch == '\\') {
                    ch = (unsigned char)fonte[p++];
                    if (!ch) { falhar(linha, "escape no fim da fonte"); break; }
                    if (ch == 'n') ch = '\n';
                    else if (ch == 'r') ch = '\r';
                    else if (ch == 't') ch = '\t';
                    else if (ch == '0') ch = 0;
                    else if (ch != '\\' && ch != '"') { falhar(linha, "escape de string nao suportado"); break; }
                }
                if (n_strings == STRING_MAX) { falhar(linha, "limite de bytes de strings atingido"); break; }
                strings[n_strings++] = (char)ch; n++;
            }
            if (!erro && !fechou) falhar(linha, "string nao terminada");
            if (erro) break;
            token_adicionar(T_STRING, off, n, linha, 0);
            continue;
        }
        if ((c == '=' && fonte[p + 1] == '=') || (c == '!' && fonte[p + 1] == '=') ||
            (c == '<' && fonte[p + 1] == '=') || (c == '>' && fonte[p + 1] == '=') ||
            (c == '&' && fonte[p + 1] == '&') || (c == '|' && fonte[p + 1] == '|')) {
            unsigned kind = c == '=' ? T_EQ : c == '!' ? T_NE : c == '<' ? T_LE :
                            c == '>' ? T_GE : c == '&' ? T_AND : T_OR;
            p += 2; token_adicionar(kind, p - 2, 2, linha, 0); continue;
        }
        if (strchr("{}();,=+-*/%!<>", c)) {
            token_adicionar(c, p, 1, linha, 0); p++; continue;
        }
        falhar(linha, "caractere nao suportado (preprocessador e literais char nao sao aceitos)");
    }
    if (!erro) token_adicionar(T_FIM, p, 0, linha, 0);
    return !erro;
}

static token_t *tk(void) { return &tokens[cursor]; }
static int eh(unsigned kind) { return tk()->kind == kind; }
static void avancar(void) { if (cursor + 1 < n_tokens) cursor++; }
static int identificador_igual(int indice, const char *texto)
{
    if (indice < 0 || (unsigned)indice >= n_tokens || tokens[indice].kind != T_ID) return 0;
    size_t n = strlen(texto);
    return tokens[indice].len == n && !memcmp(fonte + tokens[indice].off, texto, n);
}
static int atual_igual(const char *texto) { return identificador_igual((int)cursor, texto); }
static int nome_igual(int a, int b)
{
    return a >= 0 && b >= 0 && tokens[a].len == tokens[b].len &&
           !memcmp(fonte + tokens[a].off, fonte + tokens[b].off, tokens[a].len);
}
static void exigir(unsigned kind, const char *mensagem)
{
    if (erro) return;
    if (!eh(kind)) { falhar(tk()->linha, mensagem); return; }
    avancar();
}

static int palavra_tipo_auxiliar(void)
{
    return atual_igual("const") || atual_igual("volatile") || atual_igual("restrict") ||
           atual_igual("static") || atual_igual("extern") || atual_igual("register") ||
           atual_igual("auto") || atual_igual("inline") || atual_igual("_Noreturn") ||
           atual_igual("unsigned") || atual_igual("signed") || atual_igual("long") ||
           atual_igual("short");
}

static int ler_tipo(void)
{
    while (!erro && palavra_tipo_auxiliar()) avancar();
    if (!eh(T_ID)) { falhar(tk()->linha, "esperado tipo C"); return 0; }
    if (atual_igual("struct") || atual_igual("union") || atual_igual("enum")) {
        avancar();
        if (eh(T_ID)) avancar();
    } else {
        avancar(); /* int, char, void, size_t ou um typedef nao expandido */
    }
    while (eh('*')) {
        avancar();
        while (atual_igual("const") || atual_igual("volatile") || atual_igual("restrict")) avancar();
    }
    return !erro;
}

static int buscar_funcao(int nome_token)
{
    for (unsigned i = 0; i < n_funcoes; i++) if (nome_igual(funcoes[i].nome_token, nome_token)) return (int)i;
    return -1;
}

static int pre_analisar(void)
{
    cursor = 0;
    while (!erro && !eh(T_FIM)) {
        if (n_funcoes == FUNCOES_MAX) { falhar(tk()->linha, "limite de funcoes atingido"); return 0; }
        if (!ler_tipo()) return 0;
        if (!eh(T_ID)) { falhar(tk()->linha, "esperado nome de funcao"); return 0; }
        int nome = (int)cursor;
        avancar(); exigir('(', "esperado '(' na assinatura");
        funcao_t *f = &funcoes[n_funcoes];
        f->nome_token = nome; f->n_parametros = 0;
        if (atual_igual("void") && cursor + 1 < n_tokens && tokens[cursor + 1].kind == ')') {
            avancar();
        } else if (!eh(')')) {
            for (;;) {
                if (f->n_parametros == PARAMETROS_MAX) { falhar(tk()->linha, "mais de 8 parametros"); return 0; }
                if (!ler_tipo()) return 0;
                if (!eh(T_ID)) { falhar(tk()->linha, "esperado nome do parametro"); return 0; }
                f->parametros[f->n_parametros++] = (int)cursor;
                avancar();
                if (!eh(',')) break;
                avancar();
            }
        }
        exigir(')', "esperado ')' na assinatura");
        if (erro) return 0;
        if (eh(';')) { avancar(); continue; } /* prototipo: so declara o simbolo para o leitor */
        if (buscar_funcao(nome) >= 0) { falhar(tokens[nome].linha, "funcao duplicada"); return 0; }
        exigir('{', "esperado corpo de funcao '{'");
        if (erro) return 0;
        f->corpo_inicio = cursor;
        unsigned profundidade = 1;
        while (!eh(T_FIM) && profundidade) {
            if (eh('{')) profundidade++;
            else if (eh('}')) profundidade--;
            if (profundidade) avancar();
        }
        if (profundidade) { falhar(tk()->linha, "corpo de funcao nao terminado"); return 0; }
        f->corpo_fim = cursor;
        avancar();
        n_funcoes++;
    }
    int main = -1;
    for (unsigned i = 0; i < n_funcoes; i++) if (identificador_igual(funcoes[i].nome_token, "main")) main = (int)i;
    if (main < 0) { falhar(1, "funcao main ausente"); return 0; }
    if (!modo_objeto && funcoes[main].n_parametros != 0) {
        falhar(tokens[funcoes[main].nome_token].linha, "main do modo build deve receber void/nenhum argumento"); return 0;
    }
    return !erro;
}

static int novo_no(unsigned kind, unsigned op, int a, int b, unsigned valor, int nome, int primeiro, int n_args)
{
    if (n_nos == NOS_MAX) { falhar(tk()->linha, "limite de nos de expressao atingido"); return -1; }
    unsigned profundidade = 1;
    if (a >= 0 && nos[a].profundidade + 1 > profundidade) profundidade = nos[a].profundidade + 1;
    if (b >= 0 && nos[b].profundidade + 1 > profundidade) profundidade = nos[b].profundidade + 1;
    if (kind == N_CHAMADA) {
        int arg = primeiro;
        while (arg >= 0) {
            int expr = argumentos[arg].expressao;
            if (expr >= 0 && nos[expr].profundidade + 1 > profundidade) profundidade = nos[expr].profundidade + 1;
            arg = argumentos[arg].proximo;
        }
    }
    if (profundidade > 128) { falhar(tk()->linha, "expressao gera arvore de mais de 128 niveis"); return -1; }
    unsigned i = n_nos++;
    nos[i].kind = kind; nos[i].op = op; nos[i].a = a; nos[i].b = b;
    nos[i].valor = valor; nos[i].token_nome = nome;
    nos[i].primeiro_arg = primeiro; nos[i].n_args = n_args; nos[i].profundidade = profundidade;
    return (int)i;
}
static int novo_argumento(int expressao)
{
    if (n_argumentos == ARGUMENTOS_MAX) { falhar(tk()->linha, "limite de argumentos de chamada atingido"); return -1; }
    int i = (int)n_argumentos++;
    argumentos[i].expressao = expressao; argumentos[i].proximo = -1;
    return i;
}
static int precedencia(unsigned op)
{
    switch (op) {
    case T_OR: return 1;
    case T_AND: return 2;
    case T_EQ: case T_NE: return 3;
    case '<': case '>': case T_LE: case T_GE: return 4;
    case '+': case '-': return 5;
    case '*': case '/': case '%': return 6;
    default: return 0;
    }
}

static int parse_atribuicao(void);
static int parse_expressao_min(int minimo)
{
    if (++profundidade_expr > 128) { profundidade_expr--; falhar(tk()->linha, "expressao aninhada demais"); return -1; }
    int esquerda;
    if (eh('!') || eh('-') || eh('+')) {
        unsigned op = tk()->kind; avancar();
        int filho = parse_expressao_min(7);
        esquerda = novo_no(N_UNARIO, op, filho, -1, 0, -1, -1, 0);
    } else if (eh('(')) {
        avancar(); esquerda = parse_atribuicao(); exigir(')', "esperado ')' na expressao");
    } else if (eh(T_NUM)) {
        unsigned v = tk()->valor; avancar(); esquerda = novo_no(N_NUM, 0, -1, -1, v, -1, -1, 0);
    } else if (eh(T_STRING)) {
        int ti = (int)cursor; avancar(); esquerda = novo_no(N_STRING, 0, -1, -1, 0, ti, -1, 0);
    } else if (eh(T_ID)) {
        int nome = (int)cursor; avancar();
        if (eh('(')) {
            avancar(); int primeiro = -1, ultimo = -1, quantidade = 0;
            if (!eh(')')) {
                for (;;) {
                    int expr = parse_atribuicao();
                    if (erro) break;
                    int a = novo_argumento(expr);
                    if (a < 0) break;
                    if (primeiro < 0) primeiro = a;
                    else argumentos[ultimo].proximo = a;
                    ultimo = a; quantidade++;
                    if (!eh(',')) break;
                    avancar();
                }
            }
            exigir(')', "esperado ')' apos argumentos");
            int alvo = buscar_funcao(nome);
            if (alvo >= 0) {
                if (funcoes[alvo].n_parametros != (int)quantidade)
                    falhar(tokens[nome].linha, "quantidade de argumentos diferente da assinatura");
            } else if (modo_objeto) {
                alvo = -2; /* chamada externa, resolvida pelo ldagui */
            } else if (identificador_igual(nome, "puts")) {
                alvo = -1;
                if (quantidade != 1) falhar(tokens[nome].linha, "puts aceita exatamente um literal string");
            } else {
                falhar(tokens[nome].linha, "chamada externa so pode ser linkada no modo compile -c");
            }
            esquerda = novo_no(N_CHAMADA, 0, -1, -1, (unsigned)alvo, nome, primeiro, quantidade);
        } else esquerda = novo_no(N_VARIAVEL, 0, -1, -1, 0, nome, -1, 0);
    } else {
        falhar(tk()->linha, "esperado literal, variavel ou '(' em expressao");
        esquerda = -1;
    }
    while (!erro && esquerda >= 0 && precedencia(tk()->kind) >= minimo && precedencia(tk()->kind) > 0) {
        unsigned op = tk()->kind; int prec = precedencia(op); avancar();
        int direita = parse_expressao_min(prec + 1);
        esquerda = novo_no(N_BINARIO, op, esquerda, direita, 0, -1, -1, 0);
    }
    profundidade_expr--;
    return esquerda;
}
static int parse_atribuicao(void)
{
    int esquerda = parse_expressao_min(1);
    if (!erro && eh('=')) {
        unsigned linha = tk()->linha; avancar();
        if (esquerda < 0 || nos[esquerda].kind != N_VARIAVEL) { falhar(linha, "lado esquerdo de atribuicao deve ser variavel"); return -1; }
        int direita = parse_atribuicao();
        esquerda = novo_no(N_ATRIB, '=', esquerda, direita, 0, -1, -1, 0);
    }
    return esquerda;
}

static void asm_emitir(const char *formato, ...)
{
    if (erro) return;
    if (n_assembly >= ASM_MAX) { falhar(tk()->linha, "assembly gerado excede 16 KiB"); return; }
    va_list ap; va_start(ap, formato);
    int n = vsnprintf(assembly + n_assembly, ASM_MAX - n_assembly, formato, ap);
    va_end(ap);
    if (n < 0 || (unsigned)n >= ASM_MAX - n_assembly) { falhar(tk()->linha, "assembly gerado excede 16 KiB"); return; }
    n_assembly += (unsigned)n;
}
static unsigned etiqueta_nova(void) { return proxima_etiqueta++; }
static int buscar_variavel(int nome_token)
{
    for (int i = 0; i < n_variaveis; i++) if (nome_igual(variaveis[i].nome_token, nome_token)) return i;
    return -1;
}
static int adicionar_variavel(int nome_token, int parametro)
{
    if (buscar_variavel(nome_token) >= 0) { falhar(tokens[nome_token].linha, "variavel duplicada ou sombra nao suportada"); return 0; }
    if (n_variaveis == VARIAVEIS_MAX) { falhar(tokens[nome_token].linha, "limite de 32 parametros/variaveis por funcao"); return 0; }
    int i = n_variaveis++;
    variaveis[i].nome_token = nome_token;
    if (parametro) {
        int p = 0;
        while (p < funcoes[funcao_atual].n_parametros && funcoes[funcao_atual].parametros[p] != nome_token) p++;
        variaveis[i].deslocamento = 8 + p * 4;
    } else variaveis[i].deslocamento = -4 * (i - funcoes[funcao_atual].n_parametros + 1);
    return 1;
}

static void gerar_no(int indice);
static void gerar_carga_variavel(int deslocamento)
{
    if (deslocamento < 0) asm_emitir("    mov eax, [ebp%d]\n", deslocamento);
    else asm_emitir("    mov eax, [ebp+%d]\n", deslocamento);
}
static void gerar_gravacao_variavel(int deslocamento)
{
    if (deslocamento < 0) asm_emitir("    mov [ebp%d], eax\n", deslocamento);
    else asm_emitir("    mov [ebp+%d], eax\n", deslocamento);
}

static int emitir_string(int token_string)
{
    if (n_strings_emitidas == STRINGS_MAX) { falhar(tk()->linha, "limite de strings emitidas atingido"); return -1; }
    token_t *t = &tokens[token_string];
    unsigned n = t->len + 1;
    if (n_bytes_strings > STRING_MAX - n) { falhar(t->linha, "dados string excedem 8 KiB"); return -1; }
    unsigned off = n_bytes_strings;
    if (t->len) memcpy(strings_asm + off, strings + t->off, t->len);
    strings_asm[off + t->len] = modo_objeto ? 0 : '\n';
    n_bytes_strings += n;
    int id = (int)n_strings_emitidas;
    strings_emitidas[n_strings_emitidas].off = off;
    strings_emitidas[n_strings_emitidas].n = n;
    strings_emitidas[n_strings_emitidas].indice = (unsigned)id;
    n_strings_emitidas++;
    return id;
}

static void gerar_no(int indice)
{
    if (erro || indice < 0 || (unsigned)indice >= n_nos) return;
    no_t *n = &nos[indice];
    if (n->kind == N_NUM) {
        asm_emitir("    mov eax, 0x%08x\n", n->valor);
    } else if (n->kind == N_STRING) {
        int s = emitir_string(n->token_nome);
        if (s < 0) return;
        asm_emitir("    mov eax, S%d\n", s);
    } else if (n->kind == N_VARIAVEL) {
        int v = buscar_variavel(n->token_nome);
        if (v < 0) { falhar(tokens[n->token_nome].linha, "variavel nao declarada"); return; }
        gerar_carga_variavel(variaveis[v].deslocamento);
    } else if (n->kind == N_ATRIB) {
        gerar_no(n->b);
        int v = buscar_variavel(nos[n->a].token_nome);
        if (v < 0) { falhar(tokens[nos[n->a].token_nome].linha, "variavel nao declarada"); return; }
        gerar_gravacao_variavel(variaveis[v].deslocamento);
    } else if (n->kind == N_UNARIO) {
        gerar_no(n->a);
        if (n->op == '-') asm_emitir("    neg eax\n");
        else if (n->op == '!') {
            unsigned sim = etiqueta_nova(), fim = etiqueta_nova();
            asm_emitir("    cmp eax, 0\n    je L%u\n    mov eax, 0\n    jmp L%u\nL%u:\n    mov eax, 1\nL%u:\n", sim, fim, sim, fim);
        }
    } else if (n->kind == N_BINARIO) {
        if (n->op == T_AND || n->op == T_OR) {
            unsigned curto = etiqueta_nova(), fim = etiqueta_nova();
            gerar_no(n->a); asm_emitir("    cmp eax, 0\n    %s L%u\n", n->op == T_AND ? "je" : "jne", curto);
            gerar_no(n->b); asm_emitir("    cmp eax, 0\n    %s L%u\n    mov eax, %u\n    jmp L%u\nL%u:\n    mov eax, %u\nL%u:\n",
                      n->op == T_AND ? "je" : "jne", curto,
                      n->op == T_AND ? 1u : 0u, fim, curto,
                      n->op == T_AND ? 0u : 1u, fim);
        } else {
            gerar_no(n->a); asm_emitir("    push eax\n");
            gerar_no(n->b); asm_emitir("    mov ebx, eax\n    pop eax\n");
            if (n->op == '+') asm_emitir("    add eax, ebx\n");
            else if (n->op == '-') asm_emitir("    sub eax, ebx\n");
            else if (n->op == '*') asm_emitir("    imul eax, ebx\n");
            else if (n->op == '/' || n->op == '%') {
                asm_emitir("    cdq\n    idiv ebx\n");
                if (n->op == '%') asm_emitir("    mov eax, edx\n");
            } else {
                const char *salto = n->op == T_EQ ? "je" : n->op == T_NE ? "jne" :
                    n->op == '<' ? "jl" : n->op == T_LE ? "jle" :
                    n->op == '>' ? "jg" : "jge";
                unsigned sim = etiqueta_nova(), fim = etiqueta_nova();
                asm_emitir("    cmp eax, ebx\n    %s L%u\n    mov eax, 0\n    jmp L%u\nL%u:\n    mov eax, 1\nL%u:\n",
                           salto, sim, fim, sim, fim);
            }
        }
    } else if (n->kind == N_CHAMADA) {
        if ((int)n->valor == -1) {
            int arg = n->primeiro_arg;
            if (arg < 0 || argumentos[arg].expressao < 0 || nos[argumentos[arg].expressao].kind != N_STRING) {
                falhar(tokens[n->token_nome].linha, "puts aceita somente um literal string"); return;
            }
            int s = emitir_string(nos[argumentos[arg].expressao].token_nome);
            if (s < 0) return;
            asm_emitir("    mov eax, %u\n    mov ebx, %u\n    mov ecx, S%d\n    mov edx, %u\n    int 0x80\n    mov eax, 0\n",
                       (unsigned)SIS_ESCREVER, (unsigned)SAIDA, s, strings_emitidas[s].n);
        } else {
            int lista[PARAMETROS_MAX], k = 0;
            int a = n->primeiro_arg;
            while (a >= 0 && k < PARAMETROS_MAX) { lista[k++] = argumentos[a].expressao; a = argumentos[a].proximo; }
            if (a >= 0 || k != n->n_args) { falhar(tokens[n->token_nome].linha, "lista de argumentos excede o limite"); return; }
            for (int i = k - 1; i >= 0; i--) { gerar_no(lista[i]); asm_emitir("    push eax\n"); }
            if ((int)n->valor == -2) {
                if (!modo_objeto) { falhar(tokens[n->token_nome].linha, "funcao externa exige link separado"); return; }
                token_t *nome = &tokens[n->token_nome];
                asm_emitir("    call %.*s\n", (int)nome->len, fonte + nome->off);
            } else {
                asm_emitir("    call F%u\n", (unsigned)n->valor);
            }
            if (k) asm_emitir("    add esp, %u\n", (unsigned)k * 4);
        }
    }
}

static void gerar_expressao(void)
{
    int raiz = parse_atribuicao();
    if (!erro) gerar_no(raiz);
    n_nos = 0; n_argumentos = 0; profundidade_expr = 0;
}

static void declarar_variaveis(void)
{
    ler_tipo();
    for (;;) {
        if (!eh(T_ID)) { falhar(tk()->linha, "esperado nome de variavel"); return; }
        int nome = (int)cursor; avancar();
        if (!adicionar_variavel(nome, 0)) return;
        if (eh('=')) {
            avancar(); gerar_expressao();
            int v = buscar_variavel(nome);
            if (v >= 0) gerar_gravacao_variavel(variaveis[v].deslocamento);
        }
        if (!eh(',')) break;
        avancar();
    }
    exigir(';', "esperado ';' depois da declaracao");
}

static void instrucao(void)
{
    if (++profundidade_stmt > 128) { profundidade_stmt--; falhar(tk()->linha, "blocos/instrucoes aninhados demais"); return; }
    if (eh('{')) {
        avancar();
        while (!erro && !eh('}') && !eh(T_FIM)) instrucao();
        exigir('}', "bloco sem '}'");
    } else if (eh(';')) {
        avancar();
    } else if (atual_igual("int")) {
        declarar_variaveis();
    } else if (atual_igual("return")) {
        avancar();
        if (eh(';')) falhar(tk()->linha, "return em funcao int exige uma expressao");
        else gerar_expressao();
        exigir(';', "esperado ';' depois de return");
        asm_emitir("    jmp R%u\n", (unsigned)funcao_atual);
    } else if (atual_igual("if")) {
        avancar(); exigir('(', "esperado '(' apos if");
        gerar_expressao(); exigir(')', "esperado ')' depois da condicao if");
        unsigned falso = etiqueta_nova(), fim = etiqueta_nova();
        asm_emitir("    cmp eax, 0\n    je L%u\n", falso);
        instrucao();
        if (atual_igual("else")) {
            avancar(); asm_emitir("    jmp L%u\nL%u:\n", fim, falso);
            instrucao(); asm_emitir("L%u:\n", fim);
        } else asm_emitir("L%u:\n", falso);
    } else if (atual_igual("while")) {
        avancar(); unsigned inicio = etiqueta_nova(), fim = etiqueta_nova();
        asm_emitir("L%u:\n", inicio);
        exigir('(', "esperado '(' apos while");
        gerar_expressao(); exigir(')', "esperado ')' depois da condicao while");
        asm_emitir("    cmp eax, 0\n    je L%u\n", fim);
        instrucao(); asm_emitir("    jmp L%u\nL%u:\n", inicio, fim);
    } else {
        gerar_expressao(); exigir(';', "esperado ';' depois da expressao");
    }
    profundidade_stmt--;
}

static void emitir_todas_strings(void)
{
    if (!n_strings_emitidas) return;
    asm_emitir("section .rodata\n");
    for (unsigned i = 0; i < n_strings_emitidas && !erro; i++) {
        string_t *s = &strings_emitidas[i];
        asm_emitir("S%u:\n", s->indice);
        unsigned p = 0;
        while (p < s->n) {
            unsigned limite = s->n - p; if (limite > 24) limite = 24;
            asm_emitir("    db ");
            for (unsigned j = 0; j < limite; j++) {
                if (j) asm_emitir(", ");
                asm_emitir("%u", strings_asm[s->off + p + j]);
            }
            asm_emitir("\n"); p += limite;
        }
    }
}

static int gerar_programa(void)
{
    int main = -1;
    for (unsigned i = 0; i < n_funcoes; i++) if (identificador_igual(funcoes[i].nome_token, "main")) main = (int)i;
    if (modo_objeto) {
        asm_emitir("bits 32\nglobal main\nsection .text\n");
    } else {
        asm_emitir("bits 32\nglobal _start\nsection .text\n_start:\n    call F%d\n    mov ebx, eax\n    mov eax, %u\n    int 0x80\n    hlt\n", main, (unsigned)SIS_SAIR);
    }
    for (unsigned fi = 0; fi < n_funcoes && !erro; fi++) {
        funcao_atual = (int)fi; n_variaveis = 0;
        funcao_t *f = &funcoes[fi];
        for (int p = 0; p < f->n_parametros; p++) if (!adicionar_variavel(f->parametros[p], 1)) return 0;
        if (modo_objeto && (int)fi == main)
            asm_emitir("F%u:\nmain:\n    push ebp\n    mov ebp, esp\n    sub esp, 256\n", fi);
        else
            asm_emitir("F%u:\n    push ebp\n    mov ebp, esp\n    sub esp, 256\n", fi);
        cursor = f->corpo_inicio;
        while (!erro && cursor < f->corpo_fim) instrucao();
        if (!erro && cursor != f->corpo_fim) falhar(tk()->linha, "tokens sobrando no corpo da funcao");
        asm_emitir("    mov eax, 0\nR%u:\n    mov esp, ebp\n    pop ebp\n    ret\n", fi);
    }
    emitir_todas_strings();
    return !erro;
}

static int carregar_fonte(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "csagui: fonte: %s\n", strerror(errno)); return 0; }
    unsigned n = 0;
    while (n < FONTE_MAX) {
        ssize_t r = read(fd, fonte + n, FONTE_MAX - n);
        if (r < 0) { close(fd); fprintf(stderr, "csagui: erro de leitura\n"); return 0; }
        if (!r) break;
        n += (unsigned)r;
    }
    char extra;
    ssize_t excedeu = n == FONTE_MAX ? read(fd, &extra, 1) : 0;
    close(fd);
    if (excedeu != 0) { fprintf(stderr, "csagui: fonte excede 8 KiB\n"); return 0; }
    for (unsigned i = 0; i < n; i++) if (!fonte[i]) { fprintf(stderr, "csagui: byte NUL na fonte\n"); return 0; }
    fonte[n] = 0;
    return 1;
}

static int gravar_e_montar(const char *destino, const char *entrada)
{
    char temporario[64];
    snprintf(temporario, sizeof temporario, "/tmp/csagui-%u.asm", (unsigned)meu_pid());
    if (!strcmp(destino, temporario) || !strcmp(entrada, temporario)) {
        fprintf(stderr, "csagui: caminho de entrada/saida reservado para assembly temporario\n");
        return 0;
    }
    int fd = open(temporario, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { fprintf(stderr, "csagui: nao foi possivel criar assembly temporario: %s\n", strerror(errno)); return 0; }
    int w = escrever_tudo(fd, assembly, n_assembly);
    int fc = close(fd);
    if (w < 0 || fc < 0) { (void)unlink(temporario); fprintf(stderr, "csagui: erro ao gravar assembly temporario\n"); return 0; }
    char *args_exec[] = { (char *)ASAGUI_PATH, "build", "-o", (char *)destino, temporario, NULL };
    char *args_obj[] = { (char *)ASAGUI_PATH, "assemble", "-c", "-o", (char *)destino, temporario, NULL };
    int pid = executar(ASAGUI_PATH, modo_objeto ? args_obj : args_exec);
    if (pid < 0) { (void)unlink(temporario); fprintf(stderr, "csagui: nao foi possivel executar /bin/asagui\n"); return 0; }
    int codigo = -1;
    int esperado = esperar(pid, &codigo);
    (void)unlink(temporario);
    if (esperado != pid || codigo != 0) {
        fprintf(stderr, "csagui: asagui falhou (status %d)\n", codigo);
        return 0;
    }
    return 1;
}

int principal(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "version")) {
        puts("csagui 0.2: C subset -> ELF32 ET_REL; link separado com ldagui");
        return 0;
    }
    const char *destino = NULL, *entrada = NULL;
    if (argc == 6 && !strcmp(argv[1], "compile") && !strcmp(argv[2], "-c") && !strcmp(argv[3], "-o")) {
        modo_objeto = 1; destino = argv[4]; entrada = argv[5];
    } else if (argc == 5 && !strcmp(argv[1], "build") && !strcmp(argv[2], "-o")) {
        modo_objeto = 0; destino = argv[3]; entrada = argv[4];
    } else {
        fprintf(stderr, "uso: csagui compile -c -o saida.o entrada.c\n"
                        "     csagui build -o saida.elf entrada.c  (modo sem libc)\n");
        return 1;
    }
    if (!strcmp(destino, entrada)) { fprintf(stderr, "csagui: origem e destino iguais\n"); return 1; }
    if (!carregar_fonte(entrada) || !lexar() || !pre_analisar()) return 1;
    if (!gerar_programa() || erro) return 1;
    if (!gravar_e_montar(destino, entrada)) return 1;
    printf("csagui: %s gerado via asagui (%u bytes de assembly%s)\n",
           destino, n_assembly, modo_objeto ? ", ELF32 relocavel" : "");
    return 0;
}
