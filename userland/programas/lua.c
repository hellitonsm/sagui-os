/* lua.c - interpretador nativo de um subconjunto de Lua para o SaguiOS.
 *
 * O objetivo e o mesmo do gosagui: rodar dentro do proprio sistema, sem
 * libc, sem carregador dinamico e sem copiar o codigo oficial do Lua. O
 * interpretador cobre a parte da linguagem que da para usar de verdade num
 * programa pequeno: valores, variaveis, funcoes, tabelas, controle de fluxo,
 * bibliotecas basicas e arquivos.
 *
 * Decisoes que aparecem no codigo:
 *   - numeros sao inteiros de 32 bits (o PRD proibe a FPU);
 *   - / e // fazem divisao inteira, porque nao ha ponto flutuante;
 *   - a memoria dos valores nao e recolhida: o processo termina e o kernel
 *     libera tudo. Scripts longos devem evitar criar valores sem parar;
 *   - erros de sintaxe ou de execucao encerram o processo com codigo 1.
 *
 * O parser e um descendente recursivo. A execucao percorre a arvore; nao ha
 * bytecode nem compilacao para ELF, ao contrario do gosagui. */
#include "sagui.h"

#define LUA_MAX_RET        16
#define LUA_MAX_ARGS       32
#define LUA_MAX_CHAMADAS   40
#define LUA_MAX_TEXTO    1024
#define LUA_MAX_FONTE  (512 * 1024)
#define LUA_VERSAO      "0.1"

/* ==================================================================== tipos */

typedef struct texto   texto;
typedef struct tabela  tabela;
typedef struct funcao  funcao;
typedef struct ambiente ambiente;
typedef struct no      no;
typedef struct lista   lista_t;

typedef enum {
    V_NIL, V_BOOL, V_NUM, V_STR, V_TAB, V_FUNC, V_BUILTIN
} tipo_valor;

struct texto {
    int  n;
    char s[];
};

typedef struct valor {
    unsigned char tipo;
    union {
        int       b;
        int       n;
        texto    *s;
        tabela   *t;
        funcao   *f;
        int       bi;
    } u;
} valor;

typedef struct entrada {
    valor chave;
    valor val;
    int   ativo;
    int   prox;          /* proximo indice no balde do hash (0 = fim) */
} entrada;

struct tabela {
    entrada *e;
    int      n, cap;
    int     *baldes;
    int      nbaldes;
};

typedef struct variavel {
    char *nome;
    valor v;
} variavel;

struct ambiente {
    ambiente *pai;
    variavel *vars;
    int       n, cap;
    valor    *varargs;
    int       nvarargs;
    int       tem_varargs;
};

/* A lista e usada pela arvore e pelas funcoes. */
struct lista {
    no **itens;
    int  n, cap;
};

struct funcao {
    no       *corpo;
    ambiente *env;
    lista_t  *params;
    int       vararg;
    char     *nome;
};

struct no {
    int   tipo, linha, op;
    valor lit;
    char *nome;
    lista_t f;
    lista_t params;
    int   vararg;
    no   *corpo;
    lista_t chaves, valores;
    lista_t nomes;
    int   n_alvos;
    no   *senao;
};

enum {
    N_NIL, N_TRUE, N_FALSE, N_NUM, N_STR, N_VAR, N_VARARG, N_PAREN,
    N_INDEX, N_CALL, N_BINOP, N_UNOP, N_TABLE, N_FUNC,
    N_BLOCO, N_LOCAL, N_ASSIGN, N_IF, N_WHILE, N_REPEAT, N_FORNUM,
    N_FORIN, N_RETURN, N_BREAK, N_DO, N_CALLSTMT
};

enum {
    FLUXO_NORMAL, FLUXO_BREAK, FLUXO_RETURN
};

typedef struct resultado {
    int   n;
    valor v[LUA_MAX_RET];
} resultado;

/* ================================================================== memoria */

static void erro_exec(const char *fmt, ...) __attribute__((noreturn));

static int linha_atual;

static void erro_exec(const char *fmt, ...)
{
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    formatar_lista(buf, sizeof buf, fmt, ap);
    va_end(ap);
    imprimir_em(ERROS, "lua: linha %d: %s\n", linha_atual, buf);
    sair(1);
}

static void *xaloca(size_t n)
{
    void *p = aloca(n ? n : 1);
    if (!p) {
        imprimir_em(ERROS, "lua: sem memoria\n");
        sair(1);
    }
    return p;
}

static void *xrealoca(void *p, size_t n)
{
    void *q = realoca(p, n ? n : 1);
    if (!q) {
        imprimir_em(ERROS, "lua: sem memoria\n");
        sair(1);
    }
    return q;
}

static char *nome_novo(const char *s)
{
    size_t n = tam_texto(s) + 1;
    char *p = xaloca(n);
    memcpy(p, s, n);
    return p;
}

static texto *texto_novo(const char *s, int n)
{
    texto *t = xaloca(sizeof(texto) + (size_t)n + 1);
    t->n = n;
    if (n) memcpy(t->s, s, (size_t)n);
    t->s[n] = 0;
    return t;
}

static texto *texto_c(const char *s) { return texto_novo(s, (int)tam_texto(s)); }

/* ================================================================== valores */

static valor v_nil(void)            { valor v; v.tipo = V_NIL; v.u.n = 0; return v; }
static valor v_bool(int b)          { valor v; v.tipo = V_BOOL; v.u.b = b ? 1 : 0; return v; }
static valor v_num(int n)           { valor v; v.tipo = V_NUM; v.u.n = n; return v; }
static valor v_str(texto *t)        { valor v; v.tipo = V_STR; v.u.s = t; return v; }
static valor v_tab(tabela *t)       { valor v; v.tipo = V_TAB; v.u.t = t; return v; }
static valor v_func(funcao *f)      { valor v; v.tipo = V_FUNC; v.u.f = f; return v; }
static valor v_builtin(int bi)      { valor v; v.tipo = V_BUILTIN; v.u.bi = bi; return v; }
static valor v_texto(const char *s) { return v_str(texto_c(s)); }

static int valor_verdadeiro(valor v)
{
    return !(v.tipo == V_NIL || (v.tipo == V_BOOL && !v.u.b));
}

static const char *nome_tipo(valor v)
{
    switch (v.tipo) {
    case V_NIL: return "nil";
    case V_BOOL: return "boolean";
    case V_NUM: return "number";
    case V_STR: return "string";
    case V_TAB: return "table";
    case V_FUNC: case V_BUILTIN: return "function";
    default: return "?";
    }
}

static int valor_igual(valor a, valor b)
{
    if (a.tipo != b.tipo) return 0;
    switch (a.tipo) {
    case V_NIL: return 1;
    case V_BOOL: return a.u.b == b.u.b;
    case V_NUM: return a.u.n == b.u.n;
    case V_STR:
        return a.u.s->n == b.u.s->n && memcmp(a.u.s->s, b.u.s->s, (size_t)a.u.s->n) == 0;
    case V_TAB: return a.u.t == b.u.t;
    case V_FUNC: return a.u.f == b.u.f;
    case V_BUILTIN: return a.u.bi == b.u.bi;
    default: return 0;
    }
}

/* ================================================================== tabelas */

static unsigned hash_texto(const char *s, int n)
{
    unsigned h = 2166136261u;
    for (int i = 0; i < n; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

static unsigned hash_valor(valor v)
{
    switch (v.tipo) {
    case V_NIL: return 0;
    case V_BOOL: return v.u.b ? 2 : 1;
    case V_NUM: return (unsigned)v.u.n * 2654435761u;
    case V_STR: return hash_texto(v.u.s->s, v.u.s->n);
    case V_TAB: return (unsigned)(uint32_t)v.u.t;
    case V_FUNC: return (unsigned)(uint32_t)v.u.f;
    case V_BUILTIN: return (unsigned)v.u.bi + 17u;
    default: return 0;
    }
}

static tabela *tabela_nova(void)
{
    tabela *t = xaloca(sizeof *t);
    memset(t, 0, sizeof *t);
    t->nbaldes = 16;
    t->baldes = xaloca(sizeof(int) * (size_t)t->nbaldes);
    memset(t->baldes, 0, sizeof(int) * (size_t)t->nbaldes);
    return t;
}

static int tabela_indice(tabela *t, valor k)
{
    if (!t || k.tipo == V_NIL) return -1;
    unsigned h = hash_valor(k) & (unsigned)(t->nbaldes - 1);
    for (int i = t->baldes[h]; i; i = t->e[i - 1].prox)
        if (t->e[i - 1].ativo && valor_igual(t->e[i - 1].chave, k))
            return i - 1;
    return -1;
}

static void tabela_rehash(tabela *t, int novo)
{
    int *b = xaloca(sizeof(int) * (size_t)novo);
    memset(b, 0, sizeof(int) * (size_t)novo);
    for (int i = 0; i < t->n; i++) {
        if (!t->e[i].ativo) continue;
        unsigned h = hash_valor(t->e[i].chave) & (unsigned)(novo - 1);
        t->e[i].prox = b[h];
        b[h] = i + 1;
    }
    libera(t->baldes);
    t->baldes = b;
    t->nbaldes = novo;
}

static valor tabela_obter(tabela *t, valor k)
{
    int i = tabela_indice(t, k);
    return i < 0 ? v_nil() : t->e[i].val;
}

static void tabela_definir(tabela *t, valor k, valor v)
{
    if (k.tipo == V_NIL) erro_exec("indice nil numa tabela");
    int i = tabela_indice(t, k);
    if (i >= 0) { t->e[i].val = v; return; }
    if (t->n == t->cap) {
        int nc = t->cap ? t->cap * 2 : 8;
        t->e = xrealoca(t->e, sizeof(entrada) * (size_t)nc);
        t->cap = nc;
    }
    if (t->n * 2 >= t->nbaldes) tabela_rehash(t, t->nbaldes * 2);
    i = t->n++;
    t->e[i].chave = k;
    t->e[i].val = v;
    t->e[i].ativo = 1;
    unsigned h = hash_valor(k) & (unsigned)(t->nbaldes - 1);
    t->e[i].prox = t->baldes[h];
    t->baldes[h] = i + 1;
}

static int tabela_remover(tabela *t, valor k)
{
    int i = tabela_indice(t, k);
    if (i < 0) return 0;
    t->e[i].ativo = 0;
    t->e[i].val = v_nil();
    return 1;
}

static void tabela_definir_c(tabela *t, const char *nome, valor v)
{
    tabela_definir(t, v_texto(nome), v);
}

static int tabela_tamanho(tabela *t)
{
    int n = 0;
    for (;;) {
        valor k = v_num(n + 1);
        if (tabela_indice(t, k) < 0) break;
        n++;
    }
    return n;
}

static int tabela_posicao(tabela *t, valor k)
{
    for (int i = 0; i < t->n; i++)
        if (valor_igual(t->e[i].chave, k)) return i;
    return -1;
}

/* Percorre as entradas na ordem em que foram criadas. "k" nil comeca do
 * inicio; qualquer outra chave continua depois dela. A chave pode ter sido
 * removida (o corpo do laco fez "t[k] = nil"), como no Lua de verdade. */
static int tabela_proximo(tabela *t, valor k, valor *chave, valor *val)
{
    int ini = 0;
    if (k.tipo != V_NIL) {
        int i = tabela_posicao(t, k);
        if (i < 0) erro_exec("chave invalida em next");
        ini = i + 1;
    }
    for (int i = ini; i < t->n; i++) {
        if (!t->e[i].ativo) continue;
        *chave = t->e[i].chave;
        *val = t->e[i].val;
        return 1;
    }
    return 0;
}

/* ================================================================= ambientes */

static ambiente *ambiente_novo(ambiente *pai)
{
    ambiente *a = xaloca(sizeof *a);
    memset(a, 0, sizeof *a);
    a->pai = pai;
    return a;
}

static variavel *ambiente_local(ambiente *a, const char *nome)
{
    for (int i = 0; i < a->n; i++)
        if (!compara_texto(a->vars[i].nome, nome)) return &a->vars[i];
    return NULL;
}

static variavel *ambiente_achar(ambiente *a, const char *nome)
{
    for (; a; a = a->pai) {
        variavel *v = ambiente_local(a, nome);
        if (v) return v;
    }
    return NULL;
}

static void ambiente_definir(ambiente *a, const char *nome, valor v)
{
    variavel *velho = ambiente_local(a, nome);
    if (velho) { velho->v = v; return; }
    if (a->n == a->cap) {
        int nc = a->cap ? a->cap * 2 : 8;
        a->vars = xrealoca(a->vars, sizeof(variavel) * (size_t)nc);
        a->cap = nc;
    }
    a->vars[a->n].nome = nome_novo(nome);
    a->vars[a->n].v = v;
    a->n++;
}

static ambiente *ambiente_global;

static valor *ambiente_slot(ambiente *a, const char *nome)
{
    variavel *v = ambiente_achar(a, nome);
    if (v) return &v->v;
    ambiente_definir(ambiente_global, nome, v_nil());
    v = ambiente_local(ambiente_global, nome);
    return &v->v;
}

/* ==================================================================== lexer */

enum {
    T_FIM = 0, T_NOME = 256, T_NUM, T_TEXTO,
    T_AND, T_BREAK, T_DO, T_ELSE, T_ELSEIF, T_END, T_FALSE, T_FOR,
    T_FUNCTION, T_IF, T_IN, T_LOCAL, T_NIL, T_NOT, T_OR, T_REPEAT,
    T_RETURN, T_THEN, T_TRUE, T_UNTIL, T_WHILE,
    T_EQ, T_NE, T_LE, T_GE, T_CONCAT, T_VARARG, T_IDIV
};

typedef struct token {
    int   tipo, linha;
    char *texto;
    int   n;
    int   num;
} token;

static const char *fonte_lua;
static int   fonte_pos;
static int   fonte_linha;
static char  lex_buffer[LUA_MAX_TEXTO];
static token atual;
static int   linha_atual;

static void erro_sintaxe(const char *msg) __attribute__((noreturn));
static void erro_sintaxe(const char *msg)
{
    imprimir_em(ERROS, "lua: linha %d: %s\n", atual.linha ? atual.linha : fonte_linha, msg);
    sair(1);
}

static int letra(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int digito(int c) { return c >= '0' && c <= '9'; }

static int hexa(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static const struct { const char *nome; int tipo; } palavras[] = {
    { "and", T_AND }, { "break", T_BREAK }, { "do", T_DO }, { "else", T_ELSE },
    { "elseif", T_ELSEIF }, { "end", T_END }, { "false", T_FALSE }, { "for", T_FOR },
    { "function", T_FUNCTION }, { "if", T_IF }, { "in", T_IN }, { "local", T_LOCAL },
    { "nil", T_NIL }, { "not", T_NOT }, { "or", T_OR }, { "repeat", T_REPEAT },
    { "return", T_RETURN }, { "then", T_THEN }, { "true", T_TRUE }, { "until", T_UNTIL },
    { "while", T_WHILE }
};

static void pular_comentario_longo(void)
{
    /* fonte_pos esta depois de "--[[" */
    while (fonte_lua[fonte_pos] &&
           !(fonte_lua[fonte_pos] == ']' && fonte_lua[fonte_pos + 1] == ']')) {
        if (fonte_lua[fonte_pos] == '\n') fonte_linha++;
        fonte_pos++;
    }
    if (fonte_lua[fonte_pos]) fonte_pos += 2;
    else erro_sintaxe("comentario longo nao terminado");
}

static void ler_token(token *t)
{
    for (;;) {
        int c = (unsigned char)fonte_lua[fonte_pos];
        if (c == ' ' || c == '\t' || c == '\r') { fonte_pos++; continue; }
        if (c == '\n') { fonte_pos++; fonte_linha++; continue; }
        if (c == '-' && fonte_lua[fonte_pos + 1] == '-') {
            fonte_pos += 2;
            if (fonte_lua[fonte_pos] == '[' && fonte_lua[fonte_pos + 1] == '[') {
                fonte_pos += 2;
                pular_comentario_longo();
            } else {
                while (fonte_lua[fonte_pos] && fonte_lua[fonte_pos] != '\n') fonte_pos++;
            }
            continue;
        }
        break;
    }

    t->linha = fonte_linha;
    t->texto = lex_buffer;
    t->n = 0;
    t->num = 0;
    int c = (unsigned char)fonte_lua[fonte_pos];
    if (!c) { t->tipo = T_FIM; return; }

    if (letra(c)) {
        int n = 0;
        while (letra((unsigned char)fonte_lua[fonte_pos]) ||
               digito((unsigned char)fonte_lua[fonte_pos])) {
            if (n >= LUA_MAX_TEXTO - 1) erro_sintaxe("nome muito longo");
            lex_buffer[n++] = fonte_lua[fonte_pos++];
        }
        lex_buffer[n] = 0;
        t->n = n;
        t->tipo = T_NOME;
        for (unsigned i = 0; i < sizeof palavras / sizeof palavras[0]; i++)
            if (!compara_texto(lex_buffer, palavras[i].nome)) { t->tipo = palavras[i].tipo; break; }
        return;
    }

    if (digito(c)) {
        unsigned long v = 0;
        int base = 10;
        if (c == '0' && (fonte_lua[fonte_pos + 1] == 'x' || fonte_lua[fonte_pos + 1] == 'X')) {
            base = 16;
            fonte_pos += 2;
            if (!hexa((unsigned char)fonte_lua[fonte_pos])) erro_sintaxe("numero hexadecimal invalido");
        }
        for (;;) {
            int d = base == 16 ? hexa((unsigned char)fonte_lua[fonte_pos])
                               : (digito((unsigned char)fonte_lua[fonte_pos])
                                      ? fonte_lua[fonte_pos] - '0' : -1);
            if (d < 0) break;
            v = v * (unsigned)base + (unsigned)d;
            if (v > 2147483647u) erro_sintaxe("numero grande demais");
            fonte_pos++;
        }
        if (base == 10 && (fonte_lua[fonte_pos] == '.' || fonte_lua[fonte_pos] == 'e' ||
                           fonte_lua[fonte_pos] == 'E'))
            erro_sintaxe("numeros fracionarios nao sao suportados");
        t->tipo = T_NUM;
        t->num = (int)v;
        return;
    }

    if (c == '"' || c == '\'') {
        int delim = c, n = 0;
        fonte_pos++;
        while (fonte_lua[fonte_pos] && fonte_lua[fonte_pos] != delim) {
            c = (unsigned char)fonte_lua[fonte_pos++];
            if (c == '\n') erro_sintaxe("quebra de linha dentro de texto");
            if (c == '\\') {
                if (!fonte_lua[fonte_pos]) erro_sintaxe("escape nao terminado");
                c = (unsigned char)fonte_lua[fonte_pos++];
                if (c == 'a') c = 7;
                else if (c == 'b') c = 8;
                else if (c == 'f') c = 12;
                else if (c == 'n') c = '\n';
                else if (c == 'r') c = '\r';
                else if (c == 't') c = '\t';
                else if (c == 'v') c = 11;
                else if (c == '\\') c = '\\';
                else if (c == '"') c = '"';
                else if (c == '\'') c = '\'';
                else if (c == '\n') { fonte_linha++; continue; }
                else if (c == 'z') {
                    while (fonte_lua[fonte_pos] == ' ' || fonte_lua[fonte_pos] == '\t' ||
                           fonte_lua[fonte_pos] == '\r' || fonte_lua[fonte_pos] == '\n') {
                        if (fonte_lua[fonte_pos] == '\n') fonte_linha++;
                        fonte_pos++;
                    }
                    continue;
                } else if (c == 'x') {
                    int a = hexa((unsigned char)fonte_lua[fonte_pos]);
                    int b = hexa((unsigned char)fonte_lua[fonte_pos + 1]);
                    if (a < 0 || b < 0) erro_sintaxe("escape hexadecimal invalido");
                    c = a * 16 + b;
                    fonte_pos += 2;
                } else if (digito(c)) {
                    int v = c - '0';
                    for (int k = 0; k < 2 && digito((unsigned char)fonte_lua[fonte_pos]); k++)
                        v = v * 10 + (fonte_lua[fonte_pos++] - '0');
                    if (v > 255) erro_sintaxe("escape decimal grande demais");
                    c = v;
                } else {
                    /* Lua aceita \\, \", \' e tambem deixa passar escapes
                     * desconhecidos; aqui eles valem o proprio caractere. */
                }
            }
            if (n >= LUA_MAX_TEXTO - 1) erro_sintaxe("texto muito longo");
            lex_buffer[n++] = (char)c;
        }
        if (fonte_lua[fonte_pos] != delim) erro_sintaxe("texto nao terminado");
        fonte_pos++;
        lex_buffer[n] = 0;
        t->tipo = T_TEXTO;
        t->n = n;
        return;
    }

    /* Texto longo [[ ... ]]. */
    if (c == '[' && fonte_lua[fonte_pos + 1] == '[') {
        int n = 0;
        fonte_pos += 2;
        if (fonte_lua[fonte_pos] == '\n') { fonte_pos++; fonte_linha++; }
        while (fonte_lua[fonte_pos] &&
               !(fonte_lua[fonte_pos] == ']' && fonte_lua[fonte_pos + 1] == ']')) {
            if (n >= LUA_MAX_TEXTO - 1) erro_sintaxe("texto longo demais");
            if (fonte_lua[fonte_pos] == '\n') fonte_linha++;
            lex_buffer[n++] = fonte_lua[fonte_pos++];
        }
        if (!fonte_lua[fonte_pos]) erro_sintaxe("texto longo nao terminado");
        fonte_pos += 2;
        lex_buffer[n] = 0;
        t->tipo = T_TEXTO;
        t->n = n;
        return;
    }

    fonte_pos++;
    if (c == '.') {
        if (fonte_lua[fonte_pos] == '.') {
            fonte_pos++;
            if (fonte_lua[fonte_pos] == '.') { fonte_pos++; t->tipo = T_VARARG; return; }
            t->tipo = T_CONCAT;
            return;
        }
    } else if (c == '=') {
        if (fonte_lua[fonte_pos] == '=') { fonte_pos++; t->tipo = T_EQ; return; }
    } else if (c == '~') {
        if (fonte_lua[fonte_pos] == '=') { fonte_pos++; t->tipo = T_NE; return; }
        erro_sintaxe("operador '~' nao suportado");
    } else if (c == '<') {
        if (fonte_lua[fonte_pos] == '=') { fonte_pos++; t->tipo = T_LE; return; }
    } else if (c == '>') {
        if (fonte_lua[fonte_pos] == '=') { fonte_pos++; t->tipo = T_GE; return; }
    } else if (c == '/') {
        if (fonte_lua[fonte_pos] == '/') { fonte_pos++; t->tipo = T_IDIV; return; }
    }
    t->tipo = c;
}

static void avancar(void) { ler_token(&atual); }

/* =================================================================== arvore */

static no *no_novo(int tipo)
{
    no *n = xaloca(sizeof *n);
    memset(n, 0, sizeof *n);
    n->tipo = tipo;
    n->linha = atual.linha;
    return n;
}

static void lista_add(lista_t *l, no *n)
{
    if (l->n == l->cap) {
        int nc = l->cap ? l->cap * 2 : 4;
        l->itens = xrealoca(l->itens, sizeof(no *) * (size_t)nc);
        l->cap = nc;
    }
    l->itens[l->n++] = n;
}

static no *no_var(const char *nome)
{
    no *n = no_novo(N_VAR);
    n->nome = nome_novo(nome);
    return n;
}

static no *no_lit(valor v)
{
    no *n = no_novo(v.tipo == V_NIL ? N_NIL : v.tipo == V_NUM ? N_NUM : N_STR);
    n->lit = v;
    return n;
}

static no *no_bin(int op, no *a, no *b)
{
    no *n = no_novo(N_BINOP);
    n->op = op;
    lista_add(&n->f, a);
    lista_add(&n->f, b);
    return n;
}

static no *no_un(int op, no *a)
{
    no *n = no_novo(N_UNOP);
    n->op = op;
    lista_add(&n->f, a);
    return n;
}

static no *no_index(no *obj, no *chave)
{
    no *n = no_novo(N_INDEX);
    lista_add(&n->f, obj);
    lista_add(&n->f, chave);
    return n;
}

static no *no_paren(no *e)
{
    no *n = no_novo(N_PAREN);
    lista_add(&n->f, e);
    return n;
}

static no *no_bloco(void) { return no_novo(N_BLOCO); }

/* =================================================================== parser */

static void exprlista(lista_t *l);
static no *expr(void);
static no *expr_unaria(void);
static no *expr_sufixo(void);
static no *corpo_funcao(const char *nome, int metodo);
static void bloco(no *b);

static int aceitar(int tipo)
{
    if (atual.tipo == tipo) { avancar(); return 1; }
    return 0;
}

static void exigir(int tipo)
{
    if (atual.tipo != tipo) erro_sintaxe("token inesperado");
    avancar();
}

static void esperar_nome(char *destino, int max)
{
    if (atual.tipo != T_NOME) erro_sintaxe("esperado um nome");
    copia_texto(destino, atual.texto, (size_t)max);
    avancar();
}

static no *bloco_novo(void)
{
    no *b = no_bloco();
    bloco(b);
    return b;
}

static int comeca_expr(void)
{
    switch (atual.tipo) {
    case T_NIL: case T_TRUE: case T_FALSE: case T_NUM: case T_TEXTO:
    case T_NOME: case T_VARARG: case T_FUNCTION: case '(': case '{':
    case T_NOT: case '#': case '-':
        return 1;
    default:
        return 0;
    }
}

static void argumentos(lista_t *l)
{
    if (atual.tipo == '(') {
        avancar();
        if (atual.tipo != ')') exprlista(l);
        exigir(')');
    } else if (atual.tipo == '{') {
        no *t = no_novo(N_TABLE);
        avancar();
        while (atual.tipo != '}' && atual.tipo != T_FIM) {
            if (atual.tipo == '[') {
                avancar();
                no *k = expr();
                exigir(']');
                exigir('=');
                lista_add(&t->chaves, k);
                lista_add(&t->valores, expr());
            } else {
                no *e = expr();
                if (atual.tipo == '=') {
                    if (e->tipo != N_VAR) erro_sintaxe("campo de tabela invalido");
                    avancar();
                    lista_add(&t->chaves, no_lit(v_texto(e->nome)));
                    lista_add(&t->valores, expr());
                } else {
                    lista_add(&t->chaves, NULL);
                    lista_add(&t->valores, e);
                }
            }
            if (atual.tipo == ',' || atual.tipo == ';') avancar();
            else break;
        }
        exigir('}');
        lista_add(l, t);
    } else if (atual.tipo == T_TEXTO) {
        lista_add(l, no_lit(v_str(texto_novo(atual.texto, atual.n))));
        avancar();
    } else {
        erro_sintaxe("esperado argumentos");
    }
}

static no *expr_sufixo(void)
{
    no *e;
    if (atual.tipo == T_NOME) {
        e = no_var(atual.texto);
        avancar();
    } else if (atual.tipo == '(') {
        avancar();
        e = no_paren(expr());
        exigir(')');
    } else {
        erro_sintaxe("esperado nome ou '('");
        return NULL;
    }
    for (;;) {
        if (aceitar('.')) {
            char nome[LUA_MAX_TEXTO];
            esperar_nome(nome, sizeof nome);
            e = no_index(e, no_lit(v_texto(nome)));
        } else if (aceitar('[')) {
            no *k = expr();
            exigir(']');
            e = no_index(e, k);
        } else if (aceitar(':')) {
            char nome[LUA_MAX_TEXTO];
            esperar_nome(nome, sizeof nome);
            no *c = no_novo(N_CALL);
            c->op = 1;
            c->nome = nome_novo(nome);
            lista_add(&c->f, e);
            argumentos(&c->f);
            e = c;
        } else if (atual.tipo == '(' || atual.tipo == '{' || atual.tipo == T_TEXTO) {
            no *c = no_novo(N_CALL);
            lista_add(&c->f, e);
            argumentos(&c->f);
            e = c;
        } else {
            break;
        }
    }
    return e;
}

static no *expr_simples(void)
{
    if (atual.tipo == T_NIL) { avancar(); return no_lit(v_nil()); }
    if (atual.tipo == T_TRUE) { avancar(); return no_lit(v_bool(1)); }
    if (atual.tipo == T_FALSE) { avancar(); return no_lit(v_bool(0)); }
    if (atual.tipo == T_NUM) {
        int n = atual.num;
        avancar();
        return no_lit(v_num(n));
    }
    if (atual.tipo == T_TEXTO) {
        texto *t = texto_novo(atual.texto, atual.n);
        avancar();
        return no_lit(v_str(t));
    }
    if (atual.tipo == T_VARARG) { avancar(); return no_novo(N_VARARG); }
    if (atual.tipo == T_FUNCTION) {
        avancar();
        return corpo_funcao(NULL, 0);
    }
    if (atual.tipo == '{') {
        no *t = no_novo(N_TABLE);
        avancar();
        while (atual.tipo != '}' && atual.tipo != T_FIM) {
            if (atual.tipo == '[') {
                avancar();
                no *k = expr();
                exigir(']');
                exigir('=');
                lista_add(&t->chaves, k);
                lista_add(&t->valores, expr());
            } else {
                no *e = expr();
                if (atual.tipo == '=') {
                    if (e->tipo != N_VAR) erro_sintaxe("campo de tabela invalido");
                    avancar();
                    lista_add(&t->chaves, no_lit(v_texto(e->nome)));
                    lista_add(&t->valores, expr());
                } else {
                    lista_add(&t->chaves, NULL);
                    lista_add(&t->valores, e);
                }
            }
            if (atual.tipo == ',' || atual.tipo == ';') avancar();
            else break;
        }
        exigir('}');
        return t;
    }
    if (atual.tipo == T_NOME || atual.tipo == '(') return expr_sufixo();
    erro_sintaxe("expressao esperada");
    return NULL;
}

static no *expr_potencia(void)
{
    no *e = expr_simples();
    if (aceitar('^')) return no_bin('^', e, expr_unaria());
    return e;
}

static no *expr_unaria(void)
{
    if (atual.tipo == T_NOT || atual.tipo == '#' || atual.tipo == '-') {
        int op = atual.tipo;
        avancar();
        return no_un(op, expr_unaria());
    }
    return expr_potencia();
}

static no *expr_mul(void)
{
    no *e = expr_unaria();
    while (atual.tipo == '*' || atual.tipo == '/' || atual.tipo == T_IDIV || atual.tipo == '%') {
        int op = atual.tipo;
        avancar();
        e = no_bin(op, e, expr_unaria());
    }
    return e;
}

static no *expr_soma(void)
{
    no *e = expr_mul();
    while (atual.tipo == '+' || atual.tipo == '-') {
        int op = atual.tipo;
        avancar();
        e = no_bin(op, e, expr_mul());
    }
    return e;
}

static no *expr_concat(void)
{
    no *e = expr_soma();
    if (aceitar(T_CONCAT)) return no_bin(T_CONCAT, e, expr_concat());
    return e;
}

static no *expr_comp(void)
{
    no *e = expr_concat();
    while (atual.tipo == '<' || atual.tipo == '>' || atual.tipo == T_LE ||
           atual.tipo == T_GE || atual.tipo == T_EQ || atual.tipo == T_NE) {
        int op = atual.tipo;
        avancar();
        e = no_bin(op, e, expr_concat());
    }
    return e;
}

static no *expr_e(void)
{
    no *e = expr_comp();
    while (aceitar(T_AND)) e = no_bin(T_AND, e, expr_comp());
    return e;
}

static no *expr(void)
{
    no *e = expr_e();
    while (aceitar(T_OR)) e = no_bin(T_OR, e, expr_e());
    return e;
}

static void exprlista(lista_t *l)
{
    lista_add(l, expr());
    while (aceitar(',')) lista_add(l, expr());
}

static no *corpo_funcao(const char *nome, int metodo)
{
    no *f = no_novo(N_FUNC);
    f->nome = nome ? nome_novo(nome) : NULL;
    exigir('(');
    if (metodo) lista_add(&f->params, no_var("self"));
    if (atual.tipo != ')') {
        for (;;) {
            if (atual.tipo == T_VARARG) {
                f->vararg = 1;
                avancar();
                break;
            }
            if (atual.tipo != T_NOME) erro_sintaxe("esperado parametro");
            lista_add(&f->params, no_var(atual.texto));
            avancar();
            if (!aceitar(',')) break;
        }
    }
    exigir(')');
    f->corpo = bloco_novo();
    exigir(T_END);
    return f;
}

static void bloco(no *b)
{
    while (atual.tipo != T_FIM && atual.tipo != T_END && atual.tipo != T_ELSE &&
           atual.tipo != T_ELSEIF && atual.tipo != T_UNTIL) {
        if (aceitar(';')) continue;
        no *s;
        if (atual.tipo == T_LOCAL) {
            avancar();
            s = no_novo(N_LOCAL);
            if (atual.tipo == T_FUNCTION) {
                avancar();
                char nome[LUA_MAX_TEXTO];
                esperar_nome(nome, sizeof nome);
                lista_add(&s->nomes, no_var(nome));
                lista_add(&s->f, corpo_funcao(nome, 0));
                s->op = 1;
            } else {
                for (;;) {
                    char nome[LUA_MAX_TEXTO];
                    esperar_nome(nome, sizeof nome);
                    lista_add(&s->nomes, no_var(nome));
                    if (!aceitar(',')) break;
                }
                if (aceitar('=')) exprlista(&s->f);
            }
        } else if (atual.tipo == T_FUNCTION) {
            avancar();
            char nome[LUA_MAX_TEXTO];
            esperar_nome(nome, sizeof nome);
            no *alvo = no_var(nome);
            while (aceitar('.')) {
                esperar_nome(nome, sizeof nome);
                alvo = no_index(alvo, no_lit(v_texto(nome)));
            }
            int metodo = 0;
            if (aceitar(':')) {
                esperar_nome(nome, sizeof nome);
                alvo = no_index(alvo, no_lit(v_texto(nome)));
                metodo = 1;
            }
            s = no_novo(N_ASSIGN);
            s->n_alvos = 1;
            lista_add(&s->f, alvo);
            lista_add(&s->f, corpo_funcao(nome, metodo));
        } else if (atual.tipo == T_IF) {
            avancar();
            s = no_novo(N_IF);
            no *cond = expr();
            exigir(T_THEN);
            lista_add(&s->f, cond);
            lista_add(&s->f, bloco_novo());
            while (aceitar(T_ELSEIF)) {
                cond = expr();
                exigir(T_THEN);
                lista_add(&s->f, cond);
                lista_add(&s->f, bloco_novo());
            }
            if (aceitar(T_ELSE)) s->senao = bloco_novo();
            exigir(T_END);
        } else if (atual.tipo == T_WHILE) {
            avancar();
            s = no_novo(N_WHILE);
            lista_add(&s->f, expr());
            exigir(T_DO);
            s->corpo = bloco_novo();
            exigir(T_END);
        } else if (atual.tipo == T_REPEAT) {
            avancar();
            s = no_novo(N_REPEAT);
            s->corpo = bloco_novo();
            exigir(T_UNTIL);
            lista_add(&s->f, expr());
        } else if (atual.tipo == T_FOR) {
            avancar();
            char nome[LUA_MAX_TEXTO];
            esperar_nome(nome, sizeof nome);
            if (aceitar('=')) {
                s = no_novo(N_FORNUM);
                s->nome = nome_novo(nome);
                lista_add(&s->f, expr());
                exigir(',');
                lista_add(&s->f, expr());
                if (aceitar(',')) lista_add(&s->f, expr());
                exigir(T_DO);
                s->corpo = bloco_novo();
                exigir(T_END);
            } else {
                s = no_novo(N_FORIN);
                lista_add(&s->nomes, no_var(nome));
                while (aceitar(',')) {
                    esperar_nome(nome, sizeof nome);
                    lista_add(&s->nomes, no_var(nome));
                }
                exigir(T_IN);
                exprlista(&s->f);
                exigir(T_DO);
                s->corpo = bloco_novo();
                exigir(T_END);
            }
        } else if (atual.tipo == T_DO) {
            avancar();
            s = no_novo(N_DO);
            s->corpo = bloco_novo();
            exigir(T_END);
        } else if (atual.tipo == T_RETURN) {
            avancar();
            s = no_novo(N_RETURN);
            if (comeca_expr()) exprlista(&s->f);
            aceitar(';');
        } else if (atual.tipo == T_BREAK) {
            avancar();
            s = no_novo(N_BREAK);
        } else {
            no *e = expr_sufixo();
            if (atual.tipo == '=' || atual.tipo == ',') {
                s = no_novo(N_ASSIGN);
                s->n_alvos = 1;
                lista_add(&s->f, e);
                while (aceitar(',')) {
                    lista_add(&s->f, expr_sufixo());
                    s->n_alvos++;
                }
                exigir('=');
                exprlista(&s->f);
            } else {
                if (e->tipo != N_CALL) erro_sintaxe("instrucao invalida");
                s = no_novo(N_CALLSTMT);
                lista_add(&s->f, e);
            }
        }
        lista_add(&b->f, s);
        if (s->tipo == N_RETURN || s->tipo == N_BREAK) { aceitar(';'); break; }
    }
}

/* ================================================================== execucao */

static resultado retorno;
static int nivel_chamadas;

static void avaliar(no *e, ambiente *env, resultado *r);
static void avaliar_lista(lista_t *l, ambiente *env, resultado *r, int expandir);
static void chamar(valor f, valor *args, int n, resultado *r);
static int exec_bloco(no *b, ambiente *env, int novo);
static int exec_instrucao(no *s, ambiente *env);

static void res_um(resultado *r, valor v) { r->n = 1; r->v[0] = v; }
static void res_vazio(resultado *r) { r->n = 0; }

static texto *para_texto(valor v)
{
    char buf[64];
    switch (v.tipo) {
    case V_STR: return v.u.s;
    case V_NUM:
        formatar_texto(buf, sizeof buf, "%d", v.u.n);
        return texto_c(buf);
    case V_NIL: return texto_c("nil");
    case V_BOOL: return texto_c(v.u.b ? "true" : "false");
    case V_TAB:
        formatar_texto(buf, sizeof buf, "table: %p", (void *)v.u.t);
        return texto_c(buf);
    case V_FUNC:
        formatar_texto(buf, sizeof buf, "function: %p", (void *)v.u.f);
        return texto_c(buf);
    case V_BUILTIN:
        formatar_texto(buf, sizeof buf, "function: builtin %d", v.u.bi);
        return texto_c(buf);
    default: return texto_c("?");
    }
}

static int para_numero(valor v, int *ok)
{
    *ok = 1;
    if (v.tipo == V_NUM) return v.u.n;
    if (v.tipo == V_STR) {
        const char *s = v.u.s->s;
        int sinal = 1, base = 10, achou = 0;
        long long x = 0;
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
        if (*s == '-') { sinal = -1; s++; }
        else if (*s == '+') s++;
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        for (;;) {
            int d = base == 16 ? hexa((unsigned char)*s) : (digito((unsigned char)*s) ? *s - '0' : -1);
            if (d < 0) break;
            x = x * base + d;
            if (x > 2147483647ll) { *ok = 0; return 0; }
            s++;
            achou = 1;
        }
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
        if (!achou || *s) { *ok = 0; return 0; }
        return (int)(x * sinal);
    }
    *ok = 0;
    return 0;
}

static int comparar_textos(texto *a, texto *b)
{
    int n = a->n < b->n ? a->n : b->n;
    int c = memcmp(a->s, b->s, (size_t)n);
    if (c) return c;
    return a->n - b->n;
}

static valor indice(valor obj, valor chave)
{
    if (obj.tipo == V_TAB) return tabela_obter(obj.u.t, chave);
    if (obj.tipo == V_STR) {
        if (chave.tipo == V_NUM) {
            int i = chave.u.n;
            if (i < 1 || i > obj.u.s->n) return v_nil();
            return v_str(texto_novo(obj.u.s->s + i - 1, 1));
        }
        /* "s:upper()" e "s.len": os metodos vem da tabela global string. */
        variavel *sv = ambiente_achar(ambiente_global, "string");
        if (sv && sv->v.tipo == V_TAB) {
            valor f = tabela_obter(sv->v.u.t, chave);
            if (f.tipo != V_NIL) return f;
        }
        return v_nil();
    }
    erro_exec("tentativa de indexar %s", nome_tipo(obj));
    return v_nil();
}

static void atribuir_indice(valor obj, valor chave, valor v)
{
    if (obj.tipo != V_TAB) erro_exec("tentativa de indexar %s", nome_tipo(obj));
    tabela_definir(obj.u.t, chave, v);
}

static int potencia(int base, int exp)
{
    if (exp < 0) return 0;
    int r = 1;
    while (exp) {
        if (exp & 1) r *= base;
        base *= base;
        exp >>= 1;
    }
    return r;
}

static void avaliar_binop(no *e, ambiente *env, resultado *r)
{
    resultado a, b;
    avaliar(e->f.itens[0], env, &a);
    valor va = a.n ? a.v[0] : v_nil();

    if (e->op == T_AND) {
        if (!valor_verdadeiro(va)) { res_um(r, va); return; }
        avaliar(e->f.itens[1], env, &b);
        res_um(r, b.n ? b.v[0] : v_nil());
        return;
    }
    if (e->op == T_OR) {
        if (valor_verdadeiro(va)) { res_um(r, va); return; }
        avaliar(e->f.itens[1], env, &b);
        res_um(r, b.n ? b.v[0] : v_nil());
        return;
    }

    avaliar(e->f.itens[1], env, &b);
    valor vb = b.n ? b.v[0] : v_nil();

    if (e->op == T_CONCAT) {
        texto *ta = para_texto(va), *tb = para_texto(vb);
        if (va.tipo != V_STR && va.tipo != V_NUM) erro_exec("concatenacao com %s", nome_tipo(va));
        if (vb.tipo != V_STR && vb.tipo != V_NUM) erro_exec("concatenacao com %s", nome_tipo(vb));
        texto *t = xaloca(sizeof(texto) + (size_t)ta->n + (size_t)tb->n + 1);
        t->n = ta->n + tb->n;
        memcpy(t->s, ta->s, (size_t)ta->n);
        memcpy(t->s + ta->n, tb->s, (size_t)tb->n);
        t->s[t->n] = 0;
        res_um(r, v_str(t));
        return;
    }

    if (e->op == T_EQ || e->op == T_NE) {
        int igual = valor_igual(va, vb);
        res_um(r, v_bool(e->op == T_EQ ? igual : !igual));
        return;
    }

    if (e->op == '<' || e->op == '>' || e->op == T_LE || e->op == T_GE) {
        int c;
        if (va.tipo == V_NUM && vb.tipo == V_NUM) c = va.u.n < vb.u.n ? -1 : va.u.n > vb.u.n ? 1 : 0;
        else if (va.tipo == V_STR && vb.tipo == V_STR) c = comparar_textos(va.u.s, vb.u.s);
        else erro_exec("comparacao entre %s e %s", nome_tipo(va), nome_tipo(vb));
        int ok = e->op == '<' ? c < 0 : e->op == '>' ? c > 0 : e->op == T_LE ? c <= 0 : c >= 0;
        res_um(r, v_bool(ok));
        return;
    }

    int oka, okb;
    int na = para_numero(va, &oka), nb = para_numero(vb, &okb);
    if (!oka || !okb) erro_exec("aritmetica com %s", !oka ? nome_tipo(va) : nome_tipo(vb));
    int x = 0;
    switch (e->op) {
    case '+': x = na + nb; break;
    case '-': x = na - nb; break;
    case '*': x = na * nb; break;
    case '/': case T_IDIV:
        if (nb == 0) erro_exec("divisao por zero");
        x = na / nb;
        break;
    case '%':
        if (nb == 0) erro_exec("resto de divisao por zero");
        x = na % nb;
        break;
    case '^': x = potencia(na, nb); break;
    default: erro_exec("operador desconhecido");
    }
    res_um(r, v_num(x));
}

static void avaliar(no *e, ambiente *env, resultado *r)
{
    linha_atual = e->linha;
    switch (e->tipo) {
    case N_NIL: res_um(r, v_nil()); break;
    case N_TRUE: res_um(r, v_bool(1)); break;
    case N_FALSE: res_um(r, v_bool(0)); break;
    case N_NUM: case N_STR: res_um(r, e->lit); break;
    case N_VAR: {
        variavel *v = ambiente_achar(env, e->nome);
        res_um(r, v ? v->v : v_nil());
        break;
    }
    case N_VARARG: {
        ambiente *a = env;
        while (a && !a->tem_varargs) a = a->pai;
        if (!a) erro_exec("'...' fora de funcao com varargs");
        r->n = 0;
        for (int i = 0; i < a->nvarargs && r->n < LUA_MAX_RET; i++) r->v[r->n++] = a->varargs[i];
        break;
    }
    case N_PAREN:
        avaliar(e->f.itens[0], env, r);
        if (r->n == 0) res_um(r, v_nil());
        else { r->n = 1; }
        break;
    case N_INDEX: {
        resultado o, k;
        avaliar(e->f.itens[0], env, &o);
        avaliar(e->f.itens[1], env, &k);
        res_um(r, indice(o.n ? o.v[0] : v_nil(), k.n ? k.v[0] : v_nil()));
        break;
    }
    case N_CALL: {
        valor args[LUA_MAX_ARGS];
        int nargs = 0;
        if (e->op) {
            resultado o;
            avaliar(e->f.itens[0], env, &o);
            valor obj = o.n ? o.v[0] : v_nil();
            valor f = indice(obj, v_texto(e->nome));
            args[nargs++] = obj;
            for (int i = 1; i < e->f.n; i++) {
                resultado a;
                avaliar(e->f.itens[i], env, &a);
                int q = (i == e->f.n - 1) ? a.n : (a.n ? 1 : 0);
                for (int j = 0; j < q; j++) {
                    if (nargs >= LUA_MAX_ARGS) erro_exec("argumentos demais");
                    args[nargs++] = a.v[j];
                }
            }
            chamar(f, args, nargs, r);
        } else {
            resultado f;
            avaliar(e->f.itens[0], env, &f);
            for (int i = 1; i < e->f.n; i++) {
                resultado a;
                avaliar(e->f.itens[i], env, &a);
                int q = (i == e->f.n - 1) ? a.n : (a.n ? 1 : 0);
                for (int j = 0; j < q; j++) {
                    if (nargs >= LUA_MAX_ARGS) erro_exec("argumentos demais");
                    args[nargs++] = a.v[j];
                }
            }
            chamar(f.n ? f.v[0] : v_nil(), args, nargs, r);
        }
        break;
    }
    case N_BINOP: avaliar_binop(e, env, r); break;
    case N_UNOP: {
        resultado a;
        avaliar(e->f.itens[0], env, &a);
        valor v = a.n ? a.v[0] : v_nil();
        if (e->op == T_NOT) res_um(r, v_bool(!valor_verdadeiro(v)));
        else if (e->op == '#') {
            if (v.tipo == V_STR) res_um(r, v_num(v.u.s->n));
            else if (v.tipo == V_TAB) res_um(r, v_num(tabela_tamanho(v.u.t)));
            else erro_exec("comprimento de %s", nome_tipo(v));
        } else if (e->op == '-') {
            int ok;
            int n = para_numero(v, &ok);
            if (!ok) erro_exec("aritmetica com %s", nome_tipo(v));
            res_um(r, v_num(-n));
        } else erro_exec("operador unario desconhecido");
        break;
    }
    case N_TABLE: {
        tabela *t = tabela_nova();
        int array = 1;
        for (int i = 0; i < e->valores.n; i++) {
            if (!e->chaves.itens[i]) {
                resultado a;
                int expandir = (i == e->valores.n - 1) &&
                               (e->valores.itens[i]->tipo == N_CALL ||
                                e->valores.itens[i]->tipo == N_VARARG);
                avaliar(e->valores.itens[i], env, &a);
                int q = expandir ? a.n : (a.n ? 1 : 0);
                if (q == 0) { tabela_definir(t, v_num(array++), v_nil()); }
                for (int j = 0; j < q; j++)
                    tabela_definir(t, v_num(array++), a.v[j]);
            } else {
                resultado k, a;
                avaliar(e->chaves.itens[i], env, &k);
                avaliar(e->valores.itens[i], env, &a);
                valor v = a.n ? a.v[0] : v_nil();
                tabela_definir(t, k.n ? k.v[0] : v_nil(), v);
            }
        }
        res_um(r, v_tab(t));
        break;
    }
    case N_FUNC: {
        funcao *f = xaloca(sizeof *f);
        memset(f, 0, sizeof *f);
        f->corpo = e->corpo;
        f->env = env;
        f->params = &e->params;
        f->vararg = e->vararg;
        f->nome = e->nome;
        res_um(r, v_func(f));
        break;
    }
    default:
        erro_exec("expressao invalida");
    }
}

static void avaliar_lista(lista_t *l, ambiente *env, resultado *r, int expandir)
{
    r->n = 0;
    for (int i = 0; i < l->n; i++) {
        resultado a;
        avaliar(l->itens[i], env, &a);
        int q = (i == l->n - 1 && expandir) ? a.n : (a.n ? 1 : 0);
        for (int j = 0; j < q; j++) {
            if (r->n >= LUA_MAX_RET) return;
            r->v[r->n++] = a.v[j];
        }
    }
}

static int exec_instrucao(no *s, ambiente *env)
{
    linha_atual = s->linha;
    switch (s->tipo) {
    case N_LOCAL: {
        if (s->op) {
            /* "local function f": define o nome antes de criar a funcao,
             * para a recursao encontrar f no proprio ambiente. */
            ambiente_definir(env, s->nomes.itens[0]->nome, v_nil());
            resultado f;
            avaliar(s->f.itens[0], env, &f);
            ambiente_definir(env, s->nomes.itens[0]->nome, f.n ? f.v[0] : v_nil());
        } else {
            resultado vals;
            avaliar_lista(&s->f, env, &vals, 1);
            for (int i = 0; i < s->nomes.n; i++)
                ambiente_definir(env, s->nomes.itens[i]->nome,
                                 i < vals.n ? vals.v[i] : v_nil());
        }
        break;
    }
    case N_ASSIGN: {
        /* s->f guarda primeiro os alvos e depois os valores; avaliar so a
         * partir de n_alvos (senao "x = x + 1" leria o alvo como valor). */
        lista_t valores = { s->f.itens + s->n_alvos, s->f.n - s->n_alvos, 0 };
        resultado vals;
        avaliar_lista(&valores, env, &vals, 1);
        for (int i = 0; i < s->n_alvos; i++) {
            no *alvo = s->f.itens[i];
            valor v = i < vals.n ? vals.v[i] : v_nil();
            if (alvo->tipo == N_VAR) {
                *ambiente_slot(env, alvo->nome) = v;
            } else if (alvo->tipo == N_INDEX) {
                resultado o, k;
                avaliar(alvo->f.itens[0], env, &o);
                avaliar(alvo->f.itens[1], env, &k);
                atribuir_indice(o.n ? o.v[0] : v_nil(), k.n ? k.v[0] : v_nil(), v);
            } else {
                erro_exec("alvo de atribuicao invalido");
            }
        }
        break;
    }
    case N_IF: {
        for (int i = 0; i + 1 < s->f.n; i += 2) {
            resultado c;
            avaliar(s->f.itens[i], env, &c);
            if (valor_verdadeiro(c.n ? c.v[0] : v_nil())) {
                int f = exec_bloco(s->f.itens[i + 1], env, 1);
                if (f != FLUXO_NORMAL) return f;
                return FLUXO_NORMAL;
            }
        }
        if (s->senao) {
            int f = exec_bloco(s->senao, env, 1);
            if (f != FLUXO_NORMAL) return f;
        }
        break;
    }
    case N_WHILE: {
        for (;;) {
            resultado c;
            avaliar(s->f.itens[0], env, &c);
            if (!valor_verdadeiro(c.n ? c.v[0] : v_nil())) break;
            int f = exec_bloco(s->corpo, env, 1);
            if (f == FLUXO_BREAK) break;
            if (f == FLUXO_RETURN) return f;
        }
        break;
    }
    case N_REPEAT: {
        ambiente *a = ambiente_novo(env);
        for (;;) {
            int f = exec_bloco(s->corpo, a, 0);
            if (f == FLUXO_BREAK) break;
            if (f == FLUXO_RETURN) return f;
            resultado c;
            avaliar(s->f.itens[0], a, &c);
            if (valor_verdadeiro(c.n ? c.v[0] : v_nil())) break;
        }
        break;
    }
    case N_FORNUM: {
        resultado a, b, c;
        avaliar(s->f.itens[0], env, &a);
        avaliar(s->f.itens[1], env, &b);
        int ok1, ok2, ok3 = 1;
        int ini = para_numero(a.n ? a.v[0] : v_nil(), &ok1);
        int fim = para_numero(b.n ? b.v[0] : v_nil(), &ok2);
        int passo = 1;
        if (s->f.n > 2) {
            avaliar(s->f.itens[2], env, &c);
            passo = para_numero(c.n ? c.v[0] : v_nil(), &ok3);
        }
        if (!ok1 || !ok2 || !ok3) erro_exec("for numerico com valor nao numerico");
        if (passo == 0) erro_exec("passo zero no for");
        for (int i = ini; passo > 0 ? i <= fim : i >= fim; i += passo) {
            ambiente *it = ambiente_novo(env);
            ambiente_definir(it, s->nome, v_num(i));
            int f = exec_bloco(s->corpo, it, 0);
            if (f == FLUXO_BREAK) break;
            if (f == FLUXO_RETURN) return f;
        }
        break;
    }
    case N_FORIN: {
        resultado lista;
        avaliar_lista(&s->f, env, &lista, 1);
        valor iter = lista.n > 0 ? lista.v[0] : v_nil();
        valor estado = lista.n > 1 ? lista.v[1] : v_nil();
        valor controle = lista.n > 2 ? lista.v[2] : v_nil();
        for (;;) {
            valor args[2] = { estado, controle };
            resultado r;
            chamar(iter, args, 2, &r);
            if (r.n == 0 || r.v[0].tipo == V_NIL) break;
            controle = r.v[0];
            ambiente *it = ambiente_novo(env);
            for (int i = 0; i < s->nomes.n; i++)
                ambiente_definir(it, s->nomes.itens[i]->nome, i < r.n ? r.v[i] : v_nil());
            int f = exec_bloco(s->corpo, it, 0);
            if (f == FLUXO_BREAK) break;
            if (f == FLUXO_RETURN) return f;
        }
        break;
    }
    case N_RETURN: {
        /* Avalia num resultado local: chamadas dentro do "return" tambem
         * usam o retorno global, e escrever direto nele misturaria os
         * valores (ex.: "return n * fatorial(n - 1)"). */
        resultado tmp;
        avaliar_lista(&s->f, env, &tmp, 1);
        retorno = tmp;
        return FLUXO_RETURN;
    }
    case N_BREAK:
        return FLUXO_BREAK;
    case N_DO: {
        int f = exec_bloco(s->corpo, env, 1);
        if (f != FLUXO_NORMAL) return f;
        break;
    }
    case N_CALLSTMT: {
        resultado r;
        avaliar(s->f.itens[0], env, &r);
        break;
    }
    default:
        erro_exec("instrucao invalida");
    }
    return FLUXO_NORMAL;
}

static int exec_bloco(no *b, ambiente *env, int novo)
{
    if (novo) env = ambiente_novo(env);
    for (int i = 0; i < b->f.n; i++) {
        int f = exec_instrucao(b->f.itens[i], env);
        if (f != FLUXO_NORMAL) return f;
    }
    return FLUXO_NORMAL;
}

static void chamar(valor f, valor *args, int n, resultado *r)
{
    if (f.tipo == V_BUILTIN) {
        extern void lua_chamar_builtin(int bi, valor *args, int n, resultado *r);
        lua_chamar_builtin(f.u.bi, args, n, r);
        return;
    }
    if (f.tipo != V_FUNC) erro_exec("tentativa de chamar %s", nome_tipo(f));
    if (nivel_chamadas >= LUA_MAX_CHAMADAS) erro_exec("chamadas demais (recursao infinita?)");
    funcao *fn = f.u.f;
    ambiente *a = ambiente_novo(fn->env);
    for (int i = 0; i < fn->params->n; i++)
        ambiente_definir(a, fn->params->itens[i]->nome, i < n ? args[i] : v_nil());
    if (fn->vararg) {
        a->tem_varargs = 1;
        a->nvarargs = n > fn->params->n ? n - fn->params->n : 0;
        if (a->nvarargs) {
            a->varargs = xaloca(sizeof(valor) * (size_t)a->nvarargs);
            for (int i = 0; i < a->nvarargs; i++)
                a->varargs[i] = args[fn->params->n + i];
        }
    }
    nivel_chamadas++;
    int fluxo = exec_bloco(fn->corpo, a, 0);
    nivel_chamadas--;
    if (fluxo == FLUXO_RETURN) *r = retorno;
    else r->n = 0;
}

/* ================================================================ biblioteca */

enum {
    BI_PRINT = 1, BI_TYPE, BI_TOSTRING, BI_TONUMBER, BI_ASSERT, BI_ERROR,
    BI_SELECT, BI_NEXT, BI_PAIRS, BI_IPAIRS, BI_IPAIRS_ITER, BI_RAWEQUAL,
    BI_RAWGET, BI_RAWSET, BI_UNPACK, BI_DOFILE,
    BI_STR_LEN, BI_STR_SUB, BI_STR_UPPER, BI_STR_LOWER, BI_STR_REP,
    BI_STR_FIND, BI_STR_REVERSE, BI_STR_BYTE, BI_STR_CHAR, BI_STR_FORMAT,
    BI_TAB_INSERT, BI_TAB_REMOVE, BI_TAB_CONCAT, BI_TAB_SORT, BI_TAB_UNPACK,
    BI_MATH_ABS, BI_MATH_MAX, BI_MATH_MIN, BI_MATH_RANDOM, BI_MATH_RANDOMSEED,
    BI_MATH_TOINTEGER,
    BI_OS_TIME, BI_OS_CLOCK, BI_OS_DATE, BI_OS_EXIT, BI_OS_DIFFTIME,
    BI_IO_OPEN, BI_IO_WRITE, BI_IO_READ, BI_IO_CLOSE, BI_IO_LINES, BI_IO_ITER,
    BI_FILE_READ, BI_FILE_WRITE, BI_FILE_CLOSE, BI_FILE_LINES,
    BI_SAGUI_REGISTRAR, BI_SAGUI_LIMPAR, BI_SAGUI_DORMIR, BI_SAGUI_TEMPO,
    BI_SAGUI_EXECUTAR, BI_SAGUI_BIP
};

static valor arg(valor *a, int n, int i)
{
    return i < n ? a[i] : v_nil();
}

static int arg_num(valor *a, int n, int i, int padrao)
{
    int ok;
    int v = para_numero(arg(a, n, i), &ok);
    return ok ? v : padrao;
}

static const char *arg_str(valor *a, int n, int i, const char *padrao)
{
    valor v = arg(a, n, i);
    if (v.tipo == V_STR) return v.u.s->s;
    if (v.tipo == V_NUM) {
        char buf[32];
        formatar_texto(buf, sizeof buf, "%d", v.u.n);
        return texto_c(buf)->s;
    }
    return padrao;
}

static void bi_print(valor *a, int n, resultado *r)
{
    for (int i = 0; i < n; i++) {
        if (i) escrever_texto(SAIDA, "\t");
        texto *t = para_texto(a[i]);
        escrever_tudo(SAIDA, t->s, (unsigned)t->n);
    }
    escrever_texto(SAIDA, "\n");
    res_vazio(r);
}

static void bi_type(valor *a, int n, resultado *r)
{
    res_um(r, v_texto(nome_tipo(arg(a, n, 0))));
}

static void bi_tostring(valor *a, int n, resultado *r)
{
    res_um(r, v_str(para_texto(arg(a, n, 0))));
}

static void bi_tonumber(valor *a, int n, resultado *r)
{
    valor v = arg(a, n, 0);
    int ok;
    int x = para_numero(v, &ok);
    if (!ok) { res_um(r, v_nil()); return; }
    int base = n > 1 ? arg_num(a, n, 1, 10) : 10;
    if (base != 10 && v.tipo == V_STR) {
        const char *s = v.u.s->s;
        int sinal = 1;
        long long y = 0;
        int achou = 0;
        while (*s == ' ') s++;
        if (*s == '-') { sinal = -1; s++; } else if (*s == '+') s++;
        for (;;) {
            int d = hexa((unsigned char)*s);
            if (d < 0 || d >= base) break;
            y = y * base + d;
            s++;
            achou = 1;
        }
        while (*s == ' ') s++;
        if (!achou || *s) { res_um(r, v_nil()); return; }
        x = (int)(y * sinal);
    }
    res_um(r, v_num(x));
}

static void bi_assert(valor *a, int n, resultado *r)
{
    if (!valor_verdadeiro(arg(a, n, 0))) {
        if (n > 1 && a[1].tipo == V_STR) erro_exec("%s", a[1].u.s->s);
        erro_exec("assert falhou");
    }
    r->n = 0;
    for (int i = 0; i < n && r->n < LUA_MAX_RET; i++) r->v[r->n++] = a[i];
}

static void bi_error(valor *a, int n, resultado *r)
{
    (void)r;
    if (n > 0) {
        texto *t = para_texto(a[0]);
        erro_exec("%s", t->s);
    }
    erro_exec("erro");
}

static void bi_select(valor *a, int n, resultado *r)
{
    valor v = arg(a, n, 0);
    if (v.tipo == V_STR && !compara_texto(v.u.s->s, "#")) {
        res_um(r, v_num(n > 0 ? n - 1 : 0));
        return;
    }
    int i = arg_num(a, n, 0, 1);
    if (i < 0) i = n + i;
    if (i < 1) i = 1;
    r->n = 0;
    for (int k = i; k < n && r->n < LUA_MAX_RET; k++) r->v[r->n++] = a[k];
}

static void bi_next(valor *a, int n, resultado *r)
{
    valor t = arg(a, n, 0);
    if (t.tipo != V_TAB) erro_exec("next espera uma tabela");
    valor k, v;
    if (tabela_proximo(t.u.t, arg(a, n, 1), &k, &v)) {
        res_um(r, k);
        r->v[1] = v;
        r->n = 2;
    } else {
        res_um(r, v_nil());
    }
}

static void bi_pairs(valor *a, int n, resultado *r)
{
    valor t = arg(a, n, 0);
    if (t.tipo != V_TAB) erro_exec("pairs espera uma tabela");
    res_um(r, v_builtin(BI_NEXT));
    r->v[1] = t;
    r->v[2] = v_nil();
    r->n = 3;
}

static void bi_ipairs(valor *a, int n, resultado *r)
{
    valor t = arg(a, n, 0);
    if (t.tipo != V_TAB) erro_exec("ipairs espera uma tabela");
    res_um(r, v_builtin(BI_IPAIRS_ITER));
    r->v[1] = t;
    r->v[2] = v_num(0);
    r->n = 3;
}

static void bi_ipairs_iter(valor *a, int n, resultado *r)
{
    valor t = arg(a, n, 0);
    int i = arg_num(a, n, 1, 0) + 1;
    if (t.tipo != V_TAB) erro_exec("ipairs iterador invalido");
    valor v = tabela_obter(t.u.t, v_num(i));
    if (v.tipo == V_NIL) { res_um(r, v_nil()); return; }
    res_um(r, v_num(i));
    r->v[1] = v;
    r->n = 2;
}

static void bi_rawequal(valor *a, int n, resultado *r)
{
    res_um(r, v_bool(valor_igual(arg(a, n, 0), arg(a, n, 1))));
}

static void bi_rawget(valor *a, int n, resultado *r)
{
    valor t = arg(a, n, 0);
    if (t.tipo != V_TAB) erro_exec("rawget espera uma tabela");
    res_um(r, tabela_obter(t.u.t, arg(a, n, 1)));
}

static void bi_rawset(valor *a, int n, resultado *r)
{
    valor t = arg(a, n, 0);
    if (t.tipo != V_TAB) erro_exec("rawset espera uma tabela");
    tabela_definir(t.u.t, arg(a, n, 1), arg(a, n, 2));
    res_um(r, t);
}

static void bi_unpack(valor *a, int n, resultado *r)
{
    valor t = arg(a, n, 0);
    if (t.tipo != V_TAB) erro_exec("unpack espera uma tabela");
    int i = arg_num(a, n, 1, 1);
    int j = n > 2 ? arg_num(a, n, 2, tabela_tamanho(t.u.t)) : tabela_tamanho(t.u.t);
    r->n = 0;
    for (; i <= j && r->n < LUA_MAX_RET; i++) r->v[r->n++] = tabela_obter(t.u.t, v_num(i));
}

/* ---------------------------------------------------------------- texto */

static void bi_str_len(valor *a, int n, resultado *r)
{
    res_um(r, v_num(para_texto(arg(a, n, 0))->n));
}

static void bi_str_sub(valor *a, int n, resultado *r)
{
    texto *s = para_texto(arg(a, n, 0));
    int len = s->n;
    int i = arg_num(a, n, 1, 1);
    int j = arg_num(a, n, 2, -1);
    if (i < 0) i = len + i + 1;
    if (j < 0) j = len + j + 1;
    if (i < 1) i = 1;
    if (j > len) j = len;
    if (j < i) { res_um(r, v_texto("")); return; }
    res_um(r, v_str(texto_novo(s->s + i - 1, j - i + 1)));
}

static void bi_str_case(valor *a, int n, resultado *r, int alto)
{
    texto *s = para_texto(arg(a, n, 0));
    texto *t = texto_novo(s->s, s->n);
    for (int i = 0; i < t->n; i++) {
        if (alto && t->s[i] >= 'a' && t->s[i] <= 'z') t->s[i] = (char)(t->s[i] - 32);
        if (!alto && t->s[i] >= 'A' && t->s[i] <= 'Z') t->s[i] = (char)(t->s[i] + 32);
    }
    res_um(r, v_str(t));
}

static void bi_str_upper(valor *a, int n, resultado *r) { bi_str_case(a, n, r, 1); }
static void bi_str_lower(valor *a, int n, resultado *r) { bi_str_case(a, n, r, 0); }

static void bi_str_rep(valor *a, int n, resultado *r)
{
    texto *s = para_texto(arg(a, n, 0));
    int vezes = arg_num(a, n, 1, 0);
    const char *sep = arg_str(a, n, 2, "");
    int ts = (int)tam_texto(sep);
    if (vezes < 0) vezes = 0;
    long long total = (long long)s->n * vezes + (long long)ts * (vezes > 1 ? vezes - 1 : 0);
    if (total > 1024 * 1024) erro_exec("texto repetido grande demais");
    texto *t = xaloca(sizeof(texto) + (size_t)total + 1);
    t->n = 0;
    for (int i = 0; i < vezes; i++) {
        if (i && ts) { memcpy(t->s + t->n, sep, (size_t)ts); t->n += ts; }
        memcpy(t->s + t->n, s->s, (size_t)s->n);
        t->n += s->n;
    }
    t->s[t->n] = 0;
    res_um(r, v_str(t));
}

static int achar_sub(const char *s, int n, const char *sub, int m, int ini)
{
    if (m == 0) return ini <= n ? ini : -1;
    for (int i = ini; i + m <= n; i++)
        if (!memcmp(s + i, sub, (size_t)m)) return i;
    return -1;
}

static void bi_str_find(valor *a, int n, resultado *r)
{
    texto *s = para_texto(arg(a, n, 0));
    texto *sub = para_texto(arg(a, n, 1));
    int ini = arg_num(a, n, 2, 1);
    if (ini < 0) ini = s->n + ini + 1;
    if (ini < 1) ini = 1;
    int pos = achar_sub(s->s, s->n, sub->s, sub->n, ini - 1);
    if (pos < 0) { res_um(r, v_nil()); return; }
    res_um(r, v_num(pos + 1));
    r->v[1] = v_num(pos + sub->n);
    r->n = 2;
}

static void bi_str_reverse(valor *a, int n, resultado *r)
{
    texto *s = para_texto(arg(a, n, 0));
    texto *t = texto_novo(s->s, s->n);
    for (int i = 0; i < t->n / 2; i++) {
        char c = t->s[i];
        t->s[i] = t->s[t->n - 1 - i];
        t->s[t->n - 1 - i] = c;
    }
    res_um(r, v_str(t));
}

static void bi_str_byte(valor *a, int n, resultado *r)
{
    texto *s = para_texto(arg(a, n, 0));
    int i = arg_num(a, n, 1, 1);
    int j = arg_num(a, n, 2, i);
    if (i < 0) i = s->n + i + 1;
    if (j < 0) j = s->n + j + 1;
    if (i < 1) i = 1;
    if (j > s->n) j = s->n;
    r->n = 0;
    for (; i <= j && r->n < LUA_MAX_RET; i++)
        r->v[r->n++] = v_num((unsigned char)s->s[i - 1]);
}

static void bi_str_char(valor *a, int n, resultado *r)
{
    texto *t = xaloca(sizeof(texto) + (size_t)n + 1);
    t->n = n;
    for (int i = 0; i < n; i++) t->s[i] = (char)(arg_num(a, n, i, 0) & 0xFF);
    t->s[n] = 0;
    res_um(r, v_str(t));
}

static void buf_poe(char **buf, int *n, int *cap, const char *s, int m)
{
    if (*n + m + 1 > *cap) {
        int nc = *cap ? *cap : 64;
        while (nc < *n + m + 1) nc *= 2;
        *buf = xrealoca(*buf, (size_t)nc);
        *cap = nc;
    }
    memcpy(*buf + *n, s, (size_t)m);
    *n += m;
    (*buf)[*n] = 0;
}

static void bi_str_format(valor *a, int n, resultado *r)
{
    texto *fmt = para_texto(arg(a, n, 0));
    char *saida = NULL;
    int sn = 0, scap = 0, ai = 1;
    for (int i = 0; i < fmt->n; i++) {
        if (fmt->s[i] != '%') { buf_poe(&saida, &sn, &scap, &fmt->s[i], 1); continue; }
        i++;
        if (i >= fmt->n) erro_exec("formato incompleto");
        if (fmt->s[i] == '%') { buf_poe(&saida, &sn, &scap, "%", 1); continue; }
        char espec[32];
        int e = 0;
        espec[e++] = '%';
        while (i < fmt->n && (fmt->s[i] == '-' || fmt->s[i] == '0' ||
                              fmt->s[i] == '+' || fmt->s[i] == ' ' ||
                              fmt->s[i] == '#' || digito((unsigned char)fmt->s[i]) ||
                              fmt->s[i] == '.')) {
            if (e < (int)sizeof espec - 2) espec[e++] = fmt->s[i];
            i++;
        }
        if (i >= fmt->n) erro_exec("formato incompleto");
        char conv = fmt->s[i];
        valor v = arg(a, n, ai++);
        char tmp[256];
        if (conv == 's') {
            texto *t = para_texto(v);
            espec[e++] = 's'; espec[e] = 0;
            formatar_texto(tmp, sizeof tmp, espec, t->s);
        } else if (conv == 'd' || conv == 'i') {
            int ok;
            int x = para_numero(v, &ok);
            if (!ok) erro_exec("formato %c espera numero", conv);
            espec[e++] = 'd'; espec[e] = 0;
            formatar_texto(tmp, sizeof tmp, espec, x);
        } else if (conv == 'u' || conv == 'x' || conv == 'X') {
            int ok;
            int x = para_numero(v, &ok);
            if (!ok) erro_exec("formato %c espera numero", conv);
            espec[e++] = conv == 'X' ? 'x' : conv; espec[e] = 0;
            formatar_texto(tmp, sizeof tmp, espec, (unsigned)x);
            if (conv == 'X')
                for (char *p = tmp; *p; p++)
                    if (*p >= 'a' && *p <= 'f') *p = (char)(*p - 32);
        } else if (conv == 'c') {
            int ok;
            int x = para_numero(v, &ok);
            if (!ok) erro_exec("formato c espera numero");
            espec[e++] = 'c'; espec[e] = 0;
            formatar_texto(tmp, sizeof tmp, espec, x);
        } else if (conv == 'q') {
            texto *t = para_texto(v);
            char *q = NULL;
            int qn = 0, qc = 0;
            buf_poe(&q, &qn, &qc, "\"", 1);
            for (int k = 0; k < t->n; k++) {
                char c = t->s[k];
                if (c == '"' || c == '\\') { buf_poe(&q, &qn, &qc, "\\", 1); buf_poe(&q, &qn, &qc, &c, 1); }
                else if (c == '\n') buf_poe(&q, &qn, &qc, "\\n", 2);
                else if (c == '\r') buf_poe(&q, &qn, &qc, "\\r", 2);
                else if (c == '\t') buf_poe(&q, &qn, &qc, "\\t", 2);
                else buf_poe(&q, &qn, &qc, &c, 1);
            }
            buf_poe(&q, &qn, &qc, "\"", 1);
            tmp[0] = 0;
            buf_poe(&saida, &sn, &scap, q, qn);
            libera(q);
            continue;
        } else {
            erro_exec("formato nao suportado: %%%c", conv);
        }
        buf_poe(&saida, &sn, &scap, tmp, (int)tam_texto(tmp));
    }
    if (!saida) { saida = xaloca(1); saida[0] = 0; }
    res_um(r, v_str(texto_novo(saida, sn)));
    libera(saida);
}

/* --------------------------------------------------------------- tabela */

static void bi_tab_insert(valor *a, int n, resultado *r)
{
    valor t = arg(a, n, 0);
    if (t.tipo != V_TAB) erro_exec("table.insert espera uma tabela");
    int tam = tabela_tamanho(t.u.t);
    int pos, vpos;
    if (n >= 3) { pos = arg_num(a, n, 1, tam + 1); vpos = 2; }
    else { pos = tam + 1; vpos = 1; }
    if (pos < 1) pos = 1;
    if (pos > tam + 1) pos = tam + 1;
    for (int i = tam; i >= pos; i--)
        tabela_definir(t.u.t, v_num(i + 1), tabela_obter(t.u.t, v_num(i)));
    tabela_definir(t.u.t, v_num(pos), arg(a, n, vpos));
    res_vazio(r);
}

static void bi_tab_remove(valor *a, int n, resultado *r)
{
    valor t = arg(a, n, 0);
    if (t.tipo != V_TAB) erro_exec("table.remove espera uma tabela");
    int tam = tabela_tamanho(t.u.t);
    int pos = n > 1 ? arg_num(a, n, 1, tam) : tam;
    if (pos < 1 || pos > tam) { res_um(r, v_nil()); return; }
    valor v = tabela_obter(t.u.t, v_num(pos));
    for (int i = pos; i < tam; i++)
        tabela_definir(t.u.t, v_num(i), tabela_obter(t.u.t, v_num(i + 1)));
    tabela_remover(t.u.t, v_num(tam));
    res_um(r, v);
}

static void bi_tab_concat(valor *a, int n, resultado *r)
{
    valor t = arg(a, n, 0);
    if (t.tipo != V_TAB) erro_exec("table.concat espera uma tabela");
    const char *sep = arg_str(a, n, 1, "");
    int ts = (int)tam_texto(sep);
    int i = n > 2 ? arg_num(a, n, 2, 1) : 1;
    int j = n > 3 ? arg_num(a, n, 3, tabela_tamanho(t.u.t)) : tabela_tamanho(t.u.t);
    char *saida = NULL;
    int sn = 0, scap = 0;
    for (int k = i; k <= j; k++) {
        valor v = tabela_obter(t.u.t, v_num(k));
        if (v.tipo != V_STR && v.tipo != V_NUM) erro_exec("table.concat com valor %s", nome_tipo(v));
        texto *x = para_texto(v);
        if (k > i && ts) buf_poe(&saida, &sn, &scap, sep, ts);
        buf_poe(&saida, &sn, &scap, x->s, x->n);
    }
    if (!saida) { saida = xaloca(1); saida[0] = 0; }
    res_um(r, v_str(texto_novo(saida, sn)));
    libera(saida);
}

static int valor_menor(valor a, valor b)
{
    if (a.tipo == V_NUM && b.tipo == V_NUM) return a.u.n < b.u.n;
    if (a.tipo == V_STR && b.tipo == V_STR) return comparar_textos(a.u.s, b.u.s) < 0;
    erro_exec("comparacao invalida em table.sort");
    return 0;
}

static void bi_tab_sort(valor *a, int n, resultado *r)
{
    valor t = arg(a, n, 0);
    if (t.tipo != V_TAB) erro_exec("table.sort espera uma tabela");
    int tam = tabela_tamanho(t.u.t);
    for (int i = 1; i <= tam; i++) {
        for (int j = 1; j < tam; j++) {
            valor x = tabela_obter(t.u.t, v_num(j));
            valor y = tabela_obter(t.u.t, v_num(j + 1));
            int troca = valor_menor(y, x);
            if (troca) {
                tabela_definir(t.u.t, v_num(j), y);
                tabela_definir(t.u.t, v_num(j + 1), x);
            }
        }
    }
    res_vazio(r);
}

static void bi_tab_unpack(valor *a, int n, resultado *r) { bi_unpack(a, n, r); }

/* ------------------------------------------------------------------ math */

static void bi_math_abs(valor *a, int n, resultado *r)
{
    int x = arg_num(a, n, 0, 0);
    res_um(r, v_num(x < 0 ? -x : x));
}

static void bi_math_max(valor *a, int n, resultado *r)
{
    if (!n) erro_exec("math.max sem argumentos");
    int x = arg_num(a, n, 0, 0);
    for (int i = 1; i < n; i++) { int y = arg_num(a, n, i, 0); if (y > x) x = y; }
    res_um(r, v_num(x));
}

static void bi_math_min(valor *a, int n, resultado *r)
{
    if (!n) erro_exec("math.min sem argumentos");
    int x = arg_num(a, n, 0, 0);
    for (int i = 1; i < n; i++) { int y = arg_num(a, n, i, 0); if (y < x) x = y; }
    res_um(r, v_num(x));
}

static unsigned semente_lua = 12345;

static void bi_math_random(valor *a, int n, resultado *r)
{
    semente_lua = semente_lua * 1103515245u + 12345u;
    int x = (int)((semente_lua >> 8) & 0x7FFFFFFF);
    if (n == 0) { res_um(r, v_num(x % 1000000)); return; }
    int m = arg_num(a, n, 0, 1);
    int M = n > 1 ? arg_num(a, n, 1, m) : m;
    if (m > M) { int t = m; m = M; M = t; }
    if (m == M) { res_um(r, v_num(m)); return; }
    res_um(r, v_num(m + x % (M - m + 1)));
}

static void bi_math_randomseed(valor *a, int n, resultado *r)
{
    semente_lua = (unsigned)arg_num(a, n, 0, 0);
    res_vazio(r);
}

static void bi_math_tointeger(valor *a, int n, resultado *r)
{
    int ok;
    int x = para_numero(arg(a, n, 0), &ok);
    res_um(r, ok ? v_num(x) : v_nil());
}

/* -------------------------------------------------------------------- os */

static void bi_os_time(valor *a, int n, resultado *r)
{
    (void)a; (void)n;
    res_um(r, v_num((int)(tempo_ms() / 1000)));
}

static void bi_os_clock(valor *a, int n, resultado *r)
{
    (void)a; (void)n;
    res_um(r, v_num((int)tempo_ms()));
}

static void bi_os_date(valor *a, int n, resultado *r)
{
    const char *fmt = arg_str(a, n, 0, "%c");
    data_t d;
    if (ler_data(&d) < 0) { res_um(r, v_texto("?")); return; }
    char *saida = NULL;
    int sn = 0, scap = 0;
    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') { buf_poe(&saida, &sn, &scap, &fmt[i], 1); continue; }
        i++;
        char tmp[64];
        switch (fmt[i]) {
        case 'Y': formatar_texto(tmp, sizeof tmp, "%d", d.ano); break;
        case 'y': formatar_texto(tmp, sizeof tmp, "%02d", d.ano % 100); break;
        case 'm': formatar_texto(tmp, sizeof tmp, "%02d", d.mes); break;
        case 'd': formatar_texto(tmp, sizeof tmp, "%02d", d.dia); break;
        case 'H': formatar_texto(tmp, sizeof tmp, "%02d", d.hora); break;
        case 'M': formatar_texto(tmp, sizeof tmp, "%02d", d.minuto); break;
        case 'S': formatar_texto(tmp, sizeof tmp, "%02d", d.segundo); break;
        case 'c': formatar_texto(tmp, sizeof tmp, "%02d/%02d/%04d %02d:%02d:%02d",
                                d.dia, d.mes, d.ano, d.hora, d.minuto, d.segundo); break;
        case 'x': formatar_texto(tmp, sizeof tmp, "%02d/%02d/%04d", d.dia, d.mes, d.ano); break;
        case 'X': formatar_texto(tmp, sizeof tmp, "%02d:%02d:%02d", d.hora, d.minuto, d.segundo); break;
        case '%': formatar_texto(tmp, sizeof tmp, "%%"); break;
        case 0: fmt--; tmp[0] = 0; break;
        default: tmp[0] = '%'; tmp[1] = fmt[i]; tmp[2] = 0; break;
        }
        buf_poe(&saida, &sn, &scap, tmp, (int)tam_texto(tmp));
    }
    if (!saida) { saida = xaloca(1); saida[0] = 0; }
    res_um(r, v_str(texto_novo(saida, sn)));
    libera(saida);
}

static void bi_os_exit(valor *a, int n, resultado *r)
{
    (void)r;
    sair(n > 0 ? arg_num(a, n, 0, 0) : 0);
}

static void bi_os_difftime(valor *a, int n, resultado *r)
{
    res_um(r, v_num(arg_num(a, n, 0, 0) - arg_num(a, n, 1, 0)));
}

/* -------------------------------------------------------------------- io */

static int abrir_modo(const char *m)
{
    if (!compara_texto(m, "r")) return ABRIR_LER;
    if (!compara_texto(m, "w")) return ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR;
    if (!compara_texto(m, "a")) return ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_ACRESCENTAR;
    if (!compara_texto(m, "r+")) return ABRIR_LER | ABRIR_ESCREVER;
    if (!compara_texto(m, "w+")) return ABRIR_LER | ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR;
    if (!compara_texto(m, "a+")) return ABRIR_LER | ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_ACRESCENTAR;
    return -1;
}

static tabela *novo_arquivo(int fd)
{
    tabela *t = tabela_nova();
    tabela_definir_c(t, "_fd", v_num(fd));
    tabela_definir_c(t, "read", v_builtin(BI_FILE_READ));
    tabela_definir_c(t, "write", v_builtin(BI_FILE_WRITE));
    tabela_definir_c(t, "close", v_builtin(BI_FILE_CLOSE));
    tabela_definir_c(t, "lines", v_builtin(BI_FILE_LINES));
    return t;
}

static int fd_do_arquivo(valor v)
{
    if (v.tipo != V_TAB) return -1;
    valor fd = tabela_obter(v.u.t, v_texto("_fd"));
    return fd.tipo == V_NUM ? fd.u.n : -1;
}

static void ler_formatos(int fd, valor *a, int n, resultado *r)
{
    r->n = 0;
    int qtd = n == 0 ? 1 : n;
    for (int i = 0; i < qtd; i++) {
        valor f = n == 0 ? v_nil() : a[i];
        if (f.tipo == V_NUM) {
            int quer = f.u.n;
            if (quer <= 0) { r->v[r->n++] = v_texto(""); continue; }
            char *buf = xaloca((size_t)quer + 1);
            int l = ler(fd, buf, (unsigned)quer);
            if (l <= 0) { libera(buf); break; }
            buf[l] = 0;
            r->v[r->n++] = v_str(texto_novo(buf, l));
            libera(buf);
            continue;
        }
        const char *fmt = f.tipo == V_STR ? f.u.s->s : "l";
        if (fmt[0] == '*') fmt++;
        if (fmt[0] == 'a') {
            char *buf = NULL;
            int total = 0, cap = 0;
            for (;;) {
                char pedaco[512];
                int l = ler(fd, pedaco, sizeof pedaco);
                if (l < 0) { if (buf) libera(buf); erro_exec("erro de leitura"); }
                if (l == 0) break;
                buf_poe(&buf, &total, &cap, pedaco, l);
            }
            if (!buf) { break; }
            r->v[r->n++] = v_str(texto_novo(buf, total));
            libera(buf);
        } else {
            char buf[1024];
            int l = ler_linha(fd, buf, sizeof buf);
            if (l < 0) break;
            while (l > 0 && (buf[l - 1] == '\r' || buf[l - 1] == '\n')) l--;
            r->v[r->n++] = v_str(texto_novo(buf, l));
        }
        if (r->n >= LUA_MAX_RET) break;
    }
    if (r->n == 0) r->v[r->n++] = v_nil();
}

static void bi_io_open(valor *a, int n, resultado *r)
{
    const char *caminho = arg_str(a, n, 0, "");
    const char *modo = arg_str(a, n, 1, "r");
    int flags = abrir_modo(modo);
    if (flags < 0) { res_um(r, v_nil()); r->v[1] = v_texto("modo invalido"); r->n = 2; return; }
    int fd = abrir(caminho, flags);
    if (fd < 0) {
        res_um(r, v_nil());
        r->v[1] = v_texto(nome_erro(fd));
        r->n = 2;
        return;
    }
    res_um(r, v_tab(novo_arquivo(fd)));
}

static void bi_io_write(valor *a, int n, resultado *r)
{
    for (int i = 0; i < n; i++) {
        texto *t = para_texto(a[i]);
        if (escrever_tudo(SAIDA, t->s, (unsigned)t->n) < 0) erro_exec("erro de escrita");
    }
    res_um(r, v_bool(1));
}

static void bi_io_read(valor *a, int n, resultado *r) { ler_formatos(ENTRADA, a, n, r); }

static void bi_io_close(valor *a, int n, resultado *r)
{
    valor f = arg(a, n, 0);
    int fd = fd_do_arquivo(f);
    if (fd < 0) { res_um(r, v_nil()); return; }
    fechar(fd);
    tabela_definir(f.u.t, v_texto("_fd"), v_num(-1));
    res_um(r, v_bool(1));
}

static void bi_io_lines(valor *a, int n, resultado *r)
{
    const char *caminho = arg_str(a, n, 0, NULL);
    int fd;
    tabela *t;
    if (caminho) {
        fd = abrir(caminho, ABRIR_LER);
        if (fd < 0) erro_exec("io.lines: %s: %s", caminho, nome_erro(fd));
        t = novo_arquivo(fd);
    } else {
        t = novo_arquivo(ENTRADA);
    }
    res_um(r, v_builtin(BI_IO_ITER));
    r->v[1] = v_tab(t);
    r->v[2] = v_nil();
    r->n = 3;
}

static void bi_io_iter(valor *a, int n, resultado *r)
{
    int fd = fd_do_arquivo(arg(a, n, 0));
    if (fd < 0) { res_um(r, v_nil()); return; }
    char buf[1024];
    int l = ler_linha(fd, buf, sizeof buf);
    if (l < 0) { res_um(r, v_nil()); return; }
    while (l > 0 && (buf[l - 1] == '\r' || buf[l - 1] == '\n')) l--;
    res_um(r, v_str(texto_novo(buf, l)));
}

static void bi_file_read(valor *a, int n, resultado *r)
{
    int fd = fd_do_arquivo(arg(a, n, 0));
    if (fd < 0) erro_exec("arquivo fechado");
    ler_formatos(fd, n > 1 ? a + 1 : NULL, n > 1 ? n - 1 : 0, r);
}

static void bi_file_write(valor *a, int n, resultado *r)
{
    int fd = fd_do_arquivo(arg(a, n, 0));
    if (fd < 0) erro_exec("arquivo fechado");
    for (int i = 1; i < n; i++) {
        texto *t = para_texto(a[i]);
        if (escrever_tudo(fd, t->s, (unsigned)t->n) < 0) erro_exec("erro de escrita");
    }
    res_um(r, arg(a, n, 0));
}

static void bi_file_close(valor *a, int n, resultado *r) { bi_io_close(a, n, r); }

static void bi_file_lines(valor *a, int n, resultado *r)
{
    valor f = arg(a, n, 0);
    if (fd_do_arquivo(f) < 0) erro_exec("arquivo fechado");
    res_um(r, v_builtin(BI_IO_ITER));
    r->v[1] = f;
    r->v[2] = v_nil();
    r->n = 3;
}

/* ---------------------------------------------------------------- sagui */

static void bi_sagui_registrar(valor *a, int n, resultado *r)
{
    for (int i = 0; i < n; i++) {
        texto *t = para_texto(a[i]);
        registrar_texto(t->s);
    }
    res_vazio(r);
}

static void bi_sagui_limpar(valor *a, int n, resultado *r)
{
    (void)a; (void)n;
    limpar_tela();
    res_vazio(r);
}

static void bi_sagui_dormir(valor *a, int n, resultado *r)
{
    dormir((unsigned)arg_num(a, n, 0, 0));
    res_vazio(r);
}

static void bi_sagui_tempo(valor *a, int n, resultado *r)
{
    (void)a; (void)n;
    res_um(r, v_num((int)tempo_ms()));
}

static void bi_sagui_executar(valor *a, int n, resultado *r)
{
    const char *caminho = arg_str(a, n, 0, "");
    char *argv[18];
    int q = 0;
    argv[q++] = (char *)caminho;
    for (int i = 1; i < n && q < 17; i++) argv[q++] = (char *)para_texto(a[i])->s;
    argv[q] = NULL;
    int pid = executar(caminho, argv);
    if (pid < 0) {
        res_um(r, v_nil());
        r->v[1] = v_texto(nome_erro(pid));
        r->n = 2;
        return;
    }
    int codigo = -1;
    esperar(pid, &codigo);
    res_um(r, v_num(codigo));
}

static void bi_sagui_bip(valor *a, int n, resultado *r)
{
    bip(arg_num(a, n, 0, 880), arg_num(a, n, 1, 200));
    res_vazio(r);
}

/* ------------------------------------------------------------ dofile/load */

static void executar_fonte(const char *fonte, resultado *r);

static void bi_dofile(valor *a, int n, resultado *r)
{
    const char *caminho = arg_str(a, n, 0, NULL);
    if (!caminho) erro_exec("dofile espera um caminho");
    int fd = abrir(caminho, ABRIR_LER);
    if (fd < 0) erro_exec("dofile: %s: %s", caminho, nome_erro(fd));
    char *buf = NULL;
    int total = 0, cap = 0;
    for (;;) {
        char pedaco[512];
        int l = ler(fd, pedaco, sizeof pedaco);
        if (l < 0) { fechar(fd); libera(buf); erro_exec("dofile: erro de leitura"); }
        if (l == 0) break;
        if (total + l > LUA_MAX_FONTE) { fechar(fd); libera(buf); erro_exec("dofile: arquivo grande demais"); }
        buf_poe(&buf, &total, &cap, pedaco, l);
    }
    fechar(fd);
    if (!buf) { buf = xaloca(1); buf[0] = 0; }
    executar_fonte(buf, r);
    libera(buf);
}

/* ------------------------------------------------------------- despacho */

void lua_chamar_builtin(int bi, valor *a, int n, resultado *r)
{
    switch (bi) {
    case BI_PRINT: bi_print(a, n, r); break;
    case BI_TYPE: bi_type(a, n, r); break;
    case BI_TOSTRING: bi_tostring(a, n, r); break;
    case BI_TONUMBER: bi_tonumber(a, n, r); break;
    case BI_ASSERT: bi_assert(a, n, r); break;
    case BI_ERROR: bi_error(a, n, r); break;
    case BI_SELECT: bi_select(a, n, r); break;
    case BI_NEXT: bi_next(a, n, r); break;
    case BI_PAIRS: bi_pairs(a, n, r); break;
    case BI_IPAIRS: bi_ipairs(a, n, r); break;
    case BI_IPAIRS_ITER: bi_ipairs_iter(a, n, r); break;
    case BI_RAWEQUAL: bi_rawequal(a, n, r); break;
    case BI_RAWGET: bi_rawget(a, n, r); break;
    case BI_RAWSET: bi_rawset(a, n, r); break;
    case BI_UNPACK: bi_unpack(a, n, r); break;
    case BI_DOFILE: bi_dofile(a, n, r); break;
    case BI_STR_LEN: bi_str_len(a, n, r); break;
    case BI_STR_SUB: bi_str_sub(a, n, r); break;
    case BI_STR_UPPER: bi_str_upper(a, n, r); break;
    case BI_STR_LOWER: bi_str_lower(a, n, r); break;
    case BI_STR_REP: bi_str_rep(a, n, r); break;
    case BI_STR_FIND: bi_str_find(a, n, r); break;
    case BI_STR_REVERSE: bi_str_reverse(a, n, r); break;
    case BI_STR_BYTE: bi_str_byte(a, n, r); break;
    case BI_STR_CHAR: bi_str_char(a, n, r); break;
    case BI_STR_FORMAT: bi_str_format(a, n, r); break;
    case BI_TAB_INSERT: bi_tab_insert(a, n, r); break;
    case BI_TAB_REMOVE: bi_tab_remove(a, n, r); break;
    case BI_TAB_CONCAT: bi_tab_concat(a, n, r); break;
    case BI_TAB_SORT: bi_tab_sort(a, n, r); break;
    case BI_TAB_UNPACK: bi_tab_unpack(a, n, r); break;
    case BI_MATH_ABS: bi_math_abs(a, n, r); break;
    case BI_MATH_MAX: bi_math_max(a, n, r); break;
    case BI_MATH_MIN: bi_math_min(a, n, r); break;
    case BI_MATH_RANDOM: bi_math_random(a, n, r); break;
    case BI_MATH_RANDOMSEED: bi_math_randomseed(a, n, r); break;
    case BI_MATH_TOINTEGER: bi_math_tointeger(a, n, r); break;
    case BI_OS_TIME: bi_os_time(a, n, r); break;
    case BI_OS_CLOCK: bi_os_clock(a, n, r); break;
    case BI_OS_DATE: bi_os_date(a, n, r); break;
    case BI_OS_EXIT: bi_os_exit(a, n, r); break;
    case BI_OS_DIFFTIME: bi_os_difftime(a, n, r); break;
    case BI_IO_OPEN: bi_io_open(a, n, r); break;
    case BI_IO_WRITE: bi_io_write(a, n, r); break;
    case BI_IO_READ: bi_io_read(a, n, r); break;
    case BI_IO_CLOSE: bi_io_close(a, n, r); break;
    case BI_IO_LINES: bi_io_lines(a, n, r); break;
    case BI_IO_ITER: bi_io_iter(a, n, r); break;
    case BI_FILE_READ: bi_file_read(a, n, r); break;
    case BI_FILE_WRITE: bi_file_write(a, n, r); break;
    case BI_FILE_CLOSE: bi_file_close(a, n, r); break;
    case BI_FILE_LINES: bi_file_lines(a, n, r); break;
    case BI_SAGUI_REGISTRAR: bi_sagui_registrar(a, n, r); break;
    case BI_SAGUI_LIMPAR: bi_sagui_limpar(a, n, r); break;
    case BI_SAGUI_DORMIR: bi_sagui_dormir(a, n, r); break;
    case BI_SAGUI_TEMPO: bi_sagui_tempo(a, n, r); break;
    case BI_SAGUI_EXECUTAR: bi_sagui_executar(a, n, r); break;
    case BI_SAGUI_BIP: bi_sagui_bip(a, n, r); break;
    default: erro_exec("funcao interna desconhecida");
    }
}

static void definir_global_c(const char *nome, valor v)
{
    ambiente_definir(ambiente_global, nome, v);
}

static tabela *biblioteca_nova(void)
{
    tabela *t = tabela_nova();
    return t;
}

static void iniciar_bibliotecas(void)
{
    ambiente_global = ambiente_novo(NULL);

    definir_global_c("print", v_builtin(BI_PRINT));
    definir_global_c("type", v_builtin(BI_TYPE));
    definir_global_c("tostring", v_builtin(BI_TOSTRING));
    definir_global_c("tonumber", v_builtin(BI_TONUMBER));
    definir_global_c("assert", v_builtin(BI_ASSERT));
    definir_global_c("error", v_builtin(BI_ERROR));
    definir_global_c("select", v_builtin(BI_SELECT));
    definir_global_c("next", v_builtin(BI_NEXT));
    definir_global_c("pairs", v_builtin(BI_PAIRS));
    definir_global_c("ipairs", v_builtin(BI_IPAIRS));
    definir_global_c("rawequal", v_builtin(BI_RAWEQUAL));
    definir_global_c("rawget", v_builtin(BI_RAWGET));
    definir_global_c("rawset", v_builtin(BI_RAWSET));
    definir_global_c("unpack", v_builtin(BI_UNPACK));
    definir_global_c("dofile", v_builtin(BI_DOFILE));

    tabela *s = biblioteca_nova();
    tabela_definir_c(s, "len", v_builtin(BI_STR_LEN));
    tabela_definir_c(s, "sub", v_builtin(BI_STR_SUB));
    tabela_definir_c(s, "upper", v_builtin(BI_STR_UPPER));
    tabela_definir_c(s, "lower", v_builtin(BI_STR_LOWER));
    tabela_definir_c(s, "rep", v_builtin(BI_STR_REP));
    tabela_definir_c(s, "find", v_builtin(BI_STR_FIND));
    tabela_definir_c(s, "reverse", v_builtin(BI_STR_REVERSE));
    tabela_definir_c(s, "byte", v_builtin(BI_STR_BYTE));
    tabela_definir_c(s, "char", v_builtin(BI_STR_CHAR));
    tabela_definir_c(s, "format", v_builtin(BI_STR_FORMAT));
    definir_global_c("string", v_tab(s));

    tabela *t = biblioteca_nova();
    tabela_definir_c(t, "insert", v_builtin(BI_TAB_INSERT));
    tabela_definir_c(t, "remove", v_builtin(BI_TAB_REMOVE));
    tabela_definir_c(t, "concat", v_builtin(BI_TAB_CONCAT));
    tabela_definir_c(t, "sort", v_builtin(BI_TAB_SORT));
    tabela_definir_c(t, "unpack", v_builtin(BI_TAB_UNPACK));
    definir_global_c("table", v_tab(t));

    tabela *m = biblioteca_nova();
    tabela_definir_c(m, "abs", v_builtin(BI_MATH_ABS));
    tabela_definir_c(m, "max", v_builtin(BI_MATH_MAX));
    tabela_definir_c(m, "min", v_builtin(BI_MATH_MIN));
    tabela_definir_c(m, "random", v_builtin(BI_MATH_RANDOM));
    tabela_definir_c(m, "randomseed", v_builtin(BI_MATH_RANDOMSEED));
    tabela_definir_c(m, "tointeger", v_builtin(BI_MATH_TOINTEGER));
    definir_global_c("math", v_tab(m));

    tabela *o = biblioteca_nova();
    tabela_definir_c(o, "time", v_builtin(BI_OS_TIME));
    tabela_definir_c(o, "clock", v_builtin(BI_OS_CLOCK));
    tabela_definir_c(o, "date", v_builtin(BI_OS_DATE));
    tabela_definir_c(o, "exit", v_builtin(BI_OS_EXIT));
    tabela_definir_c(o, "difftime", v_builtin(BI_OS_DIFFTIME));
    definir_global_c("os", v_tab(o));

    tabela *io = biblioteca_nova();
    tabela_definir_c(io, "open", v_builtin(BI_IO_OPEN));
    tabela_definir_c(io, "write", v_builtin(BI_IO_WRITE));
    tabela_definir_c(io, "read", v_builtin(BI_IO_READ));
    tabela_definir_c(io, "close", v_builtin(BI_IO_CLOSE));
    tabela_definir_c(io, "lines", v_builtin(BI_IO_LINES));
    definir_global_c("io", v_tab(io));

    tabela *sg = biblioteca_nova();
    tabela_definir_c(sg, "registrar", v_builtin(BI_SAGUI_REGISTRAR));
    tabela_definir_c(sg, "limpar_tela", v_builtin(BI_SAGUI_LIMPAR));
    tabela_definir_c(sg, "dormir", v_builtin(BI_SAGUI_DORMIR));
    tabela_definir_c(sg, "tempo", v_builtin(BI_SAGUI_TEMPO));
    tabela_definir_c(sg, "executar", v_builtin(BI_SAGUI_EXECUTAR));
    tabela_definir_c(sg, "bip", v_builtin(BI_SAGUI_BIP));
    definir_global_c("sagui", v_tab(sg));
}

/* ==================================================================== main */

static void executar_fonte(const char *fonte, resultado *r)
{
    fonte_lua = fonte;
    fonte_pos = 0;
    fonte_linha = 1;
    linha_atual = 1;
    avancar();
    no *b = no_bloco();
    bloco(b);
    if (atual.tipo != T_FIM) erro_sintaxe("sobrou texto depois do fim");
    int fluxo = exec_bloco(b, ambiente_global, 0);
    if (fluxo == FLUXO_BREAK) erro_exec("break fora de um laco");
    if (r) {
        if (fluxo == FLUXO_RETURN) *r = retorno;
        else r->n = 0;
    }
}

static char *ler_arquivo(const char *caminho, int *tam)
{
    int fd = abrir(caminho, ABRIR_LER);
    if (fd < 0) {
        imprimir_em(ERROS, "lua: %s: %s\n", caminho, nome_erro(fd));
        return NULL;
    }
    char *buf = NULL;
    int total = 0, cap = 0;
    for (;;) {
        char pedaco[1024];
        int l = ler(fd, pedaco, sizeof pedaco);
        if (l < 0) { fechar(fd); libera(buf); return NULL; }
        if (l == 0) break;
        if (total + l > LUA_MAX_FONTE) {
            fechar(fd); libera(buf);
            imprimir_em(ERROS, "lua: %s: arquivo grande demais\n", caminho);
            return NULL;
        }
        buf_poe(&buf, &total, &cap, pedaco, l);
    }
    fechar(fd);
    if (!buf) { buf = xaloca(1); buf[0] = 0; }
    for (int i = 0; i < total; i++) {
        if (!buf[i]) {
            libera(buf);
            imprimir_em(ERROS, "lua: %s: byte NUL na fonte\n", caminho);
            return NULL;
        }
    }
    *tam = total;
    return buf;
}

static void preparar_arg(int argc, char **argv, int inicio)
{
    tabela *t = tabela_nova();
    tabela_definir(t, v_num(0), v_texto(argv[inicio]));
    for (int i = inicio + 1; i < argc; i++)
        tabela_definir(t, v_num(i - inicio), v_texto(argv[i]));
    definir_global_c("arg", v_tab(t));
}

static void repl(void)
{
    char linha[512];
    for (;;) {
        imprimir("> ");
        int n = ler_linha(ENTRADA, linha, sizeof linha);
        if (n < 0) break;
        if (n == 0) continue;
        if (linha[0] == '=') {
            char *fonte = xaloca((size_t)n + 8);
            copia_texto(fonte, "return ", (size_t)n + 8);
            junta_texto(fonte, linha + 1, (size_t)n + 8);
            resultado r;
            executar_fonte(fonte, &r);
            for (int i = 0; i < r.n; i++) {
                if (i) escrever_texto(SAIDA, "\t");
                texto *t = para_texto(r.v[i]);
                escrever_tudo(SAIDA, t->s, (unsigned)t->n);
            }
            escrever_texto(SAIDA, "\n");
            libera(fonte);
        } else {
            executar_fonte(linha, NULL);
        }
    }
    escrever_texto(SAIDA, "\n");
}

int principal(int argc, char **argv)
{
    if (argc == 2 && (!compara_texto(argv[1], "-v") || !compara_texto(argv[1], "--version"))) {
        imprimir("lua %s: subconjunto nativo do SaguiOS (inteiros, sem FPU)\n", LUA_VERSAO);
        return 0;
    }
    if (argc == 1) {
        iniciar_bibliotecas();
        repl();
        return 0;
    }
    if (argc >= 3 && !compara_texto(argv[1], "-e")) {
        iniciar_bibliotecas();
        preparar_arg(argc, argv, 1);
        executar_fonte(argv[2], NULL);
        return 0;
    }
    if (argv[1][0] == '-') {
        imprimir_em(ERROS, "uso: lua [arquivo.lua [argumentos...]] | lua -e \"codigo\"\n");
        return 1;
    }
    iniciar_bibliotecas();
    preparar_arg(argc, argv, 1);
    int tam = 0;
    char *fonte = ler_arquivo(argv[1], &tam);
    if (!fonte) return 1;
    executar_fonte(fonte, NULL);
    libera(fonte);
    return 0;
}
