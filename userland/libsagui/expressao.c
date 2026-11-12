/* expressao.c - o motor de expressões regulares do SaguiOS (H50).
 *
 * Duas etapas: o padrão vira uma árvore (sintaxe BRE ou ERE, com as
 * extensões do grep do Linux) e a árvore vira um NFA de Thompson.  A busca
 * é a simulação de Pike: em cada posição do texto há uma lista de estados
 * ativos, cada um com o ponto onde o trecho começou.  Assim o mesmo passe
 * responde "casa?" e devolve o trecho mais à esquerda (e o mais longo entre
 * os que começam no mesmo ponto), que é o que o grep -o e o --color usam.
 *
 * Tudo é feito com vetores de tamanho fixo dentro de expressao_t: não há
 * alocação, nem libc, e o programa pode compilar vários padrões (um
 * expressao_t para cada) guardando só o que precisa. */
#include "expressao.h"

typedef unsigned char u8;

/* ------------------------------------------------------------------ básicos */

static void zerar(void *d, int n)
{
    u8 *p = (u8 *)d;
    while (n-- > 0) *p++ = 0;
}

static void copiar(void *d, const void *s, int n)
{
    u8 *a = (u8 *)d;
    const u8 *b = (const u8 *)s;
    while (n-- > 0) *a++ = *b++;
}

/* Minúscula e maiúscula no Latin-1 (as mesmas letras que as telas usam). */
static int minuscula(int c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    return c;
}

static int maiuscula(int c)
{
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    return c;
}

static int eh_letra(int c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return 1;
    if (c >= 0xC0 && c <= 0xFF && c != 0xD7 && c != 0xF7) return 1;
    return 0;
}

static int eh_palavra(int c)
{
    return eh_letra(c) || (c >= '0' && c <= '9') || c == '_';
}

static int eh_espaco(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' ||
           c == '\f' || c == '\r';
}

/* ------------------------------------------------------------------ classes */

static void set_add(u8 *s, int c) { s[c >> 3] |= (u8)(1u << (c & 7)); }
static int  set_tem(const u8 *s, int c) { return (s[c >> 3] >> (c & 7)) & 1; }
static void set_zerar(u8 *s) { zerar(s, 32); }
static void set_negar(u8 *s) { for (int i = 0; i < 32; i++) s[i] = (u8)~s[i]; }

/* -i: acrescenta as duas formas de cada letra que já está no conjunto. */
static void set_caso(u8 *s)
{
    u8 antes[32];
    copiar(antes, s, 32);
    for (int c = 0; c < 256; c++)
        if (set_tem(antes, c)) {
            set_add(s, minuscula(c));
            set_add(s, maiuscula(c));
        }
}

static int nome_igual(const char *a, int n, const char *b)
{
    int i = 0;
    while (i < n && b[i] && a[i] == b[i]) i++;
    return i == n && b[i] == 0;
}

/* Preenche o conjunto com uma classe POSIX [[:nome:]].  1 = conhecida. */
static int classe_posix(u8 *s, const char *nome, int n)
{
    for (int c = 0; c < 256; c++) {
        int ok = 0;
        if (nome_igual(nome, n, "alnum"))       ok = eh_letra(c) || (c >= '0' && c <= '9');
        else if (nome_igual(nome, n, "alpha"))  ok = eh_letra(c);
        else if (nome_igual(nome, n, "ascii"))  ok = c < 128;
        else if (nome_igual(nome, n, "blank"))  ok = c == ' ' || c == '\t';
        else if (nome_igual(nome, n, "cntrl"))  ok = c < 32 || c == 127;
        else if (nome_igual(nome, n, "digit"))  ok = c >= '0' && c <= '9';
        else if (nome_igual(nome, n, "graph"))  ok = (c > 32 && c < 127) || (c >= 0xA1 && c <= 0xFF);
        else if (nome_igual(nome, n, "lower"))  ok = (c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7);
        else if (nome_igual(nome, n, "print"))  ok = c == ' ' || (c > 32 && c < 127) || (c >= 0xA1 && c <= 0xFF);
        else if (nome_igual(nome, n, "punct"))  ok = ((c > 32 && c < 127) || (c >= 0xA1 && c <= 0xFF)) && !eh_letra(c) && !(c >= '0' && c <= '9');
        else if (nome_igual(nome, n, "space"))  ok = eh_espaco(c);
        else if (nome_igual(nome, n, "upper"))  ok = (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7);
        else if (nome_igual(nome, n, "word"))   ok = eh_palavra(c);
        else if (nome_igual(nome, n, "xdigit")) ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
        else return 0;
        if (ok) set_add(s, c);
    }
    return 1;
}

/* ------------------------------------------------------------------ análise */

typedef struct {
    expressao_t *e;
    const char  *p;
    const char  *base;
    int          opcoes;
    int          prof;
    const char  *erro;
    int          erro_pos;
} analisador_t;

static int falha(analisador_t *a, const char *msg)
{
    if (!a->erro) {
        a->erro = msg;
        a->erro_pos = (int)(a->p - a->base);
    }
    return -1;
}

static int novo_no(analisador_t *a, int tipo)
{
    expressao_t *e = a->e;
    if (e->qtd_nos >= EXP_MAX_NOS) return falha(a, "expressão grande demais");
    int i = e->qtd_nos++;
    exp_no_t *n = &e->nos[i];
    n->tipo = (u8)tipo;
    n->classe = 0;
    n->esq = n->dir = -1;
    n->min = n->max = 0;
    return i;
}

static int novo_car(analisador_t *a, int c)
{
    int i = novo_no(a, (a->opcoes & EXP_CASO_INSENSIVEL) ? EXP_NO_CAR_CI : EXP_NO_CAR);
    if (i >= 0) a->e->nos[i].classe = (u8)c;
    return i;
}

static int nova_classe(analisador_t *a, const u8 *set)
{
    expressao_t *e = a->e;
    for (int i = 0; i < e->qtd_classes; i++) {
        int igual = 1;
        for (int k = 0; k < 32; k++)
            if (e->classes[i].bits[k] != set[k]) { igual = 0; break; }
        if (igual) return i;
    }
    if (e->qtd_classes >= EXP_MAX_CLASSES) return falha(a, "classes demais na expressão");
    int i = e->qtd_classes++;
    copiar(e->classes[i].bits, set, 32);
    return i;
}

static int em_fim_ramo(analisador_t *a, const char *p)
{
    if (!*p) return 1;
    if (a->opcoes & EXP_ESTENDIDA) return *p == ')' || *p == '|';
    return p[0] == '\\' && (p[1] == ')' || p[1] == '|');
}

static int no_ancora(int tipo)
{
    return tipo == EXP_NO_BOL || tipo == EXP_NO_EOL ||
           tipo == EXP_NO_PALAVRA || tipo == EXP_NO_NAO_PALAVRA ||
           tipo == EXP_NO_INI_PALAVRA || tipo == EXP_NO_FIM_PALAVRA ||
           tipo == EXP_NO_INI_OK || tipo == EXP_NO_FIM_OK;
}

static int no_repetivel(int tipo)
{
    return !no_ancora(tipo);
}

static int ler_alternativa(analisador_t *a);

/* Lê uma classe [ ... ].  O '[' já foi consumido. */
static int ler_classe(analisador_t *a)
{
    u8 set[32];
    int negado = 0, primeiro = 1;
    set_zerar(set);
    if (*a->p == '^') { negado = 1; a->p++; }
    for (;;) {
        if (!*a->p) return falha(a, "classe sem ]");
        if (*a->p == ']' && !primeiro) { a->p++; break; }
        if (a->p[0] == '[' && a->p[1] == ':') {
            const char *ini = a->p + 2, *f = ini;
            while (*f && !(f[0] == ':' && f[1] == ']')) f++;
            if (!*f) return falha(a, "classe [[:...:]] sem fechamento");
            if (!classe_posix(set, ini, (int)(f - ini)))
                return falha(a, "classe [[:...:]] desconhecida");
            a->p = f + 2;
            primeiro = 0;
            continue;
        }
        if (a->p[0] == '[' && (a->p[1] == '.' || a->p[1] == '='))
            return falha(a, "colação e equivalência não são suportadas");
        int c = (u8)*a->p++;
        if (c == '-' && primeiro) { set_add(set, '-'); primeiro = 0; continue; }
        if (a->p[0] == '-' && a->p[1] && a->p[1] != ']') {
            a->p++;
            int d = (u8)*a->p++;
            if (d < c) return falha(a, "intervalo inválido");
            for (int k = c; k <= d; k++) set_add(set, k);
        } else {
            set_add(set, c);
        }
        primeiro = 0;
    }
    if (a->opcoes & EXP_CASO_INSENSIVEL) set_caso(set);
    if (negado) set_negar(set);
    int idx = nova_classe(a, set);
    if (idx < 0) return -1;
    int n = novo_no(a, EXP_NO_CLASSE);
    if (n >= 0) a->e->nos[n].classe = (u8)idx;
    return n;
}

/* Depois da barra invertida. */
static int ler_escape(analisador_t *a)
{
    a->p++;
    int c = (u8)*a->p;
    if (!c) return falha(a, "barra invertida no fim");
    if (c >= '1' && c <= '9')
        return falha(a, "referências anteriores (\\1) não são suportadas");
    if (c == 'w' || c == 'W' || c == 's' || c == 'S') {
        u8 set[32];
        set_zerar(set);
        for (int k = 0; k < 256; k++)
            if ((c == 'w' || c == 'W') ? eh_palavra(k) : eh_espaco(k)) set_add(set, k);
        if (c == 'W' || c == 'S') set_negar(set);
        int idx = nova_classe(a, set);
        if (idx < 0) return -1;
        int n = novo_no(a, EXP_NO_CLASSE);
        if (n >= 0) a->e->nos[n].classe = (u8)idx;
        a->p++;
        return n;
    }
    if (c == 'b' || c == 'B' || c == '<' || c == '>') {
        int tipo = c == 'b' ? EXP_NO_PALAVRA :
                   c == 'B' ? EXP_NO_NAO_PALAVRA :
                   c == '<' ? EXP_NO_INI_PALAVRA : EXP_NO_FIM_PALAVRA;
        a->p++;
        return novo_no(a, tipo);
    }
    /* Como o grep: uma barra antes de um caractere comum é o próprio
     * caractere (\. vira '.', \t vira 't', \\ vira '\'). */
    a->p++;
    return novo_car(a, c);
}

static int ler_quantificador(analisador_t *a, int atomo)
{
    const char *p = a->p;
    int ere = a->opcoes & EXP_ESTENDIDA;
    int min, max;

    if (*p == '*') { min = 0; max = -1; p++; }
    else if (ere ? *p == '+' : (p[0] == '\\' && p[1] == '+')) {
        min = 1; max = -1; p += ere ? 1 : 2;
    } else if (ere ? *p == '?' : (p[0] == '\\' && p[1] == '?')) {
        min = 0; max = 1; p += ere ? 1 : 2;
    } else if (ere ? *p == '{' : (p[0] == '\\' && p[1] == '{')) {
        const char *q = p + (ere ? 1 : 2);
        int m = 0, n = 0, tem_m = 0, tem_n = 0, virgula = 0;
        while (*q >= '0' && *q <= '9') {
            if (m > 999) return falha(a, "intervalo grande demais");
            m = m * 10 + (*q++ - '0');
            tem_m = 1;
        }
        if (*q == ',') {
            virgula = 1;
            q++;
            while (*q >= '0' && *q <= '9') {
                if (n > 999) return falha(a, "intervalo grande demais");
                n = n * 10 + (*q++ - '0');
                tem_n = 1;
            }
        }
        int fechou = ere ? (*q == '}') : (q[0] == '\\' && q[1] == '}');
        if (!fechou || (!tem_m && !tem_n)) {
            if (!ere) return falha(a, "chave sem fechamento");
            return atomo;   /* ERE: um '{' que não é intervalo é literal */
        }
        if (!tem_m) min = 0;
        else min = m;
        if (virgula) max = tem_n ? n : -1;
        else max = m;
        if (max >= 0 && max < min) return falha(a, "intervalo inválido");
        p = q + (ere ? 1 : 2);
    } else {
        return atomo;
    }

    int n = novo_no(a, EXP_NO_REPETIR);
    if (n < 0) return -1;
    a->e->nos[n].esq = (short)atomo;
    a->e->nos[n].min = (short)min;
    a->e->nos[n].max = (short)max;
    a->p = p;
    return n;
}

static int ler_atomo(analisador_t *a, int inicio_ramo)
{
    int c = (u8)*a->p;
    int ere = a->opcoes & EXP_ESTENDIDA;

    if (ere) {
        if (c == '(') {
            a->p++;
            if (++a->prof > 64) { a->prof--; return falha(a, "expressão aninhada demais"); }
            int n = ler_alternativa(a);
            a->prof--;
            if (n < 0) return -1;
            if (*a->p != ')') return falha(a, "grupo sem )");
            a->p++;
            return n;
        }
        if (c == '[') { a->p++; return ler_classe(a); }
        if (c == '.') { a->p++; return novo_no(a, EXP_NO_PONTO); }
        /* No ERE o grep trata ^ e $ como âncoras em qualquer posição. */
        if (c == '^') { a->p++; return novo_no(a, EXP_NO_BOL); }
        if (c == '$') { a->p++; return novo_no(a, EXP_NO_EOL); }
        if (c == '\\') return ler_escape(a);
        a->p++;
        return novo_car(a, c);
    }

    if (c == '\\') {
        int d = (u8)a->p[1];
        if (d == '(') {
            a->p += 2;
            if (++a->prof > 64) { a->prof--; return falha(a, "expressão aninhada demais"); }
            int n = ler_alternativa(a);
            a->prof--;
            if (n < 0) return -1;
            if (!(a->p[0] == '\\' && a->p[1] == ')')) return falha(a, "grupo sem \\)");
            a->p += 2;
            return n;
        }
        if (d == ')') return falha(a, "\\) inesperado");
        if (d == '|') return falha(a, "\\| inesperado");
        if (d == '{' && inicio_ramo) { a->p += 2; return novo_car(a, '{'); }
        if (d == '+' || d == '?') { a->p += 2; return novo_car(a, d); }
        if (d == '}') { a->p += 2; return novo_car(a, '}'); }
        return ler_escape(a);
    }
    if (c == '[') { a->p++; return ler_classe(a); }
    if (c == '.') { a->p++; return novo_no(a, EXP_NO_PONTO); }
    if (c == '^' && inicio_ramo) { a->p++; return novo_no(a, EXP_NO_BOL); }
    if (c == '$' && em_fim_ramo(a, a->p + 1)) { a->p++; return novo_no(a, EXP_NO_EOL); }
    a->p++;
    return novo_car(a, c);
}

static int ler_concatenacao(analisador_t *a)
{
    int resultado = -1;
    int inicio_ramo = 1;
    while (*a->p && !em_fim_ramo(a, a->p)) {
        int atomo = ler_atomo(a, inicio_ramo);
        if (atomo < 0) return -1;
        int tipo = a->e->nos[atomo].tipo;
        int pode = no_repetivel(tipo);
        /* No ERE o grep aceita * + ? depois de âncoras e limites de palavra
         * (\b?, \>+, $*); no BRE o quantificador vira literal. */
        if (!pode && (a->opcoes & EXP_ESTENDIDA) && no_ancora(tipo)) {
            int q = (u8)*a->p;
            if (q == '*' || q == '+' || q == '?') pode = 1;
        }
        if (pode) {
            /* Como o grep: um quantificador pode seguir outro (a**,
             * a*?, a{2}{3}); ele vale para a repetição inteira. */
            for (;;) {
                int novo = ler_quantificador(a, atomo);
                if (novo < 0) return -1;
                if (novo == atomo) break;
                atomo = novo;
            }
        }
        if (resultado < 0) {
            resultado = atomo;
        } else {
            int n = novo_no(a, EXP_NO_CONCAT);
            if (n < 0) return -1;
            a->e->nos[n].esq = (short)resultado;
            a->e->nos[n].dir = (short)atomo;
            resultado = n;
        }
        inicio_ramo = 0;
    }
    if (resultado < 0) resultado = novo_no(a, EXP_NO_VAZIO);
    return resultado;
}

static int ler_alternativa(analisador_t *a)
{
    int esq = ler_concatenacao(a);
    if (esq < 0) return -1;
    for (;;) {
        const char *p = a->p;
        int tem = (a->opcoes & EXP_ESTENDIDA) ? (*p == '|')
                                             : (p[0] == '\\' && p[1] == '|');
        if (!tem) break;
        a->p += (a->opcoes & EXP_ESTENDIDA) ? 1 : 2;
        int dir = ler_concatenacao(a);
        if (dir < 0) return -1;
        int n = novo_no(a, EXP_NO_ALT);
        if (n < 0) return -1;
        a->e->nos[n].esq = (short)esq;
        a->e->nos[n].dir = (short)dir;
        esq = n;
    }
    return esq;
}

/* ------------------------------------------------------------------ NFA */

typedef struct {
    expressao_t *e;
    int pend_cod[EXP_MAX_ESTADOS];    /* instrução * 2 + campo (0 = saida1, 1 = saida2) */
    int pend_prox[EXP_MAX_ESTADOS];   /* próxima entrada da lista; -1 = fim */
    int qtd_pend;
    int erro;
} compilador_t;

/* Um trecho do NFA: "ini" é onde ele começa e "head" é a lista encadeada
 * (em pend_cod/pend_prox) das saídas que ainda não foram ligadas. */
typedef struct { int ini, head; } fragmento_t;

static int emitir(compilador_t *c, int tipo)
{
    if (c->e->qtd_inst >= EXP_MAX_ESTADOS) { c->erro = 1; return -1; }
    int i = c->e->qtd_inst++;
    exp_inst_t *x = &c->e->inst[i];
    x->tipo = (u8)tipo;
    x->classe = 0;
    x->saida1 = x->saida2 = -1;
    return i;
}

/* Registra uma saída ainda não ligada (a instrução e qual dos dois campos). */
static int novo_pend(compilador_t *c, int inst, int campo)
{
    if (c->qtd_pend >= EXP_MAX_ESTADOS) { c->erro = 1; return -1; }
    int i = c->qtd_pend++;
    c->pend_cod[i] = inst * 2 + campo;
    c->pend_prox[i] = -1;
    return i;
}

/* Liga todas as saídas de uma lista ao mesmo destino. */
static void ligar_lista(compilador_t *c, int head, int destino)
{
    for (int i = head; i >= 0; i = c->pend_prox[i]) {
        int cod = c->pend_cod[i], s = cod >> 1, campo = cod & 1;
        if (campo) c->e->inst[s].saida2 = (short)destino;
        else       c->e->inst[s].saida1 = (short)destino;
    }
}

/* Junta duas listas de saídas pendentes (as duas continuam pendentes). */
static int juntar_listas(compilador_t *c, int a, int b)
{
    if (a < 0) return b;
    if (b < 0) return a;
    int i = a;
    while (c->pend_prox[i] >= 0) i = c->pend_prox[i];
    c->pend_prox[i] = b;
    return a;
}

static fragmento_t fragmento_vazio(compilador_t *c)
{
    fragmento_t f;
    f.ini = -1;
    f.head = -1;
    int i = emitir(c, EXP_I_NOP);
    if (i < 0) return f;
    int p = novo_pend(c, i, 0);
    if (p < 0) return f;
    f.ini = i;
    f.head = p;
    return f;
}

static fragmento_t compilar_no(compilador_t *c, int idx);

static fragmento_t compilar_repetir(compilador_t *c, int idx)
{
    expressao_t *e = c->e;
    exp_no_t *no = &e->nos[idx];
    int min = no->min, max = no->max;
    fragmento_t f;

    if (min == 0) {
        f = fragmento_vazio(c);
        if (c->erro) return f;
    } else {
        f = compilar_no(c, no->esq);
        if (c->erro) return f;
        for (int k = 1; k < min; k++) {
            fragmento_t g = compilar_no(c, no->esq);
            if (c->erro) return f;
            ligar_lista(c, f.head, g.ini);
            f.head = g.head;
        }
    }

    if (max < 0) {
        /* f seguido de "filho*": um único desvio que volta ao filho. */
        fragmento_t g = compilar_no(c, no->esq);
        if (c->erro) return f;
        int s = emitir(c, EXP_I_DIVIDE);
        if (s < 0) return f;
        e->inst[s].saida1 = (short)g.ini;
        ligar_lista(c, g.head, s);
        int p = novo_pend(c, s, 1);
        if (p < 0) return f;
        ligar_lista(c, f.head, s);
        f.head = p;
    } else {
        /* (filho){0,q}: uma cadeia de opcionais encaixados, montada de
         * dentro para fora.  Assim "a{2,4}" é "aa(a(a)?)?" e não deixa
         * pular um "a" do meio. */
        fragmento_t o;
        o.ini = -1;
        o.head = -1;
        for (int k = max - min; k >= 1; k--) {
            fragmento_t g = compilar_no(c, no->esq);
            if (c->erro) return f;
            fragmento_t corpo = g;
            if (o.ini >= 0) {
                ligar_lista(c, g.head, o.ini);
                corpo.head = o.head;
            }
            int s = emitir(c, EXP_I_DIVIDE);
            if (s < 0) return f;
            e->inst[s].saida1 = (short)corpo.ini;
            int p = novo_pend(c, s, 1);
            if (p < 0) return f;
            corpo.head = juntar_listas(c, corpo.head, p);
            corpo.ini = s;
            o = corpo;
        }
        if (o.ini >= 0) {
            ligar_lista(c, f.head, o.ini);
            f.head = o.head;
        }
    }
    return f;
}

static fragmento_t compilar_no(compilador_t *c, int idx)
{
    expressao_t *e = c->e;
    exp_no_t *no = &e->nos[idx];
    fragmento_t f;
    f.ini = -1;
    f.head = -1;

    switch (no->tipo) {
    case EXP_NO_VAZIO:
        return fragmento_vazio(c);
    case EXP_NO_CAR:
    case EXP_NO_CAR_CI:
    case EXP_NO_CLASSE:
    case EXP_NO_PONTO: {
        int tipo = no->tipo == EXP_NO_CAR ? EXP_I_CAR :
                   no->tipo == EXP_NO_CAR_CI ? EXP_I_CAR_CI :
                   no->tipo == EXP_NO_CLASSE ? EXP_I_CLASSE : EXP_I_PONTO;
        int i = emitir(c, tipo);
        if (i < 0) return f;
        e->inst[i].classe = no->classe;
        int p = novo_pend(c, i, 0);
        if (p < 0) return f;
        f.ini = i;
        f.head = p;
        return f;
    }
    case EXP_NO_BOL:
    case EXP_NO_EOL:
    case EXP_NO_PALAVRA:
    case EXP_NO_NAO_PALAVRA:
    case EXP_NO_INI_PALAVRA:
    case EXP_NO_FIM_PALAVRA:
    case EXP_NO_INI_OK:
    case EXP_NO_FIM_OK: {
        int tipo = no->tipo == EXP_NO_BOL ? EXP_I_BOL :
                   no->tipo == EXP_NO_EOL ? EXP_I_EOL :
                   no->tipo == EXP_NO_PALAVRA ? EXP_I_PALAVRA :
                   no->tipo == EXP_NO_NAO_PALAVRA ? EXP_I_NAO_PALAVRA :
                   no->tipo == EXP_NO_INI_PALAVRA ? EXP_I_INI_PALAVRA :
                   no->tipo == EXP_NO_FIM_PALAVRA ? EXP_I_FIM_PALAVRA :
                   no->tipo == EXP_NO_INI_OK ? EXP_I_INI_OK : EXP_I_FIM_OK;
        int i = emitir(c, tipo);
        if (i < 0) return f;
        int p = novo_pend(c, i, 0);
        if (p < 0) return f;
        f.ini = i;
        f.head = p;
        return f;
    }
    case EXP_NO_CONCAT: {
        /* Achata a corrente à esquerda (evita recursão funda em padrões
         * longos, como os do -F com muitos caracteres). */
        short ramos[EXP_MAX_NOS];
        int q = 0, atual = idx;
        while (atual >= 0 && e->nos[atual].tipo == EXP_NO_CONCAT) {
            ramos[q++] = e->nos[atual].dir;
            atual = e->nos[atual].esq;
        }
        f = compilar_no(c, atual);
        if (c->erro) return f;
        for (int i = q - 1; i >= 0; i--) {
            fragmento_t g = compilar_no(c, ramos[i]);
            if (c->erro) return f;
            ligar_lista(c, f.head, g.ini);
            f.head = g.head;
        }
        return f;
    }
    case EXP_NO_ALT: {
        short ramos[EXP_MAX_NOS];
        int q = 0, atual = idx;
        while (atual >= 0 && e->nos[atual].tipo == EXP_NO_ALT) {
            ramos[q++] = e->nos[atual].dir;
            atual = e->nos[atual].esq;
        }
        f = compilar_no(c, atual);
        if (c->erro) return f;
        for (int i = q - 1; i >= 0; i--) {
            fragmento_t g = compilar_no(c, ramos[i]);
            if (c->erro) return f;
            int s = emitir(c, EXP_I_DIVIDE);
            if (s < 0) return f;
            e->inst[s].saida1 = (short)f.ini;
            e->inst[s].saida2 = (short)g.ini;
            f.ini = s;
            f.head = juntar_listas(c, f.head, g.head);
        }
        return f;
    }
    case EXP_NO_REPETIR:
        return compilar_repetir(c, idx);
    default:
        c->erro = 1;
        return f;
    }
}

/* Junta dois nós numa concatenação (para os invólucros de -w e -x). */
static int concatenar(analisador_t *a, int x, int y)
{
    int n = novo_no(a, EXP_NO_CONCAT);
    if (n < 0) return -1;
    a->e->nos[n].esq = (short)x;
    a->e->nos[n].dir = (short)y;
    return n;
}

int expressao_compilar(expressao_t *e, const char *padrao, int opcoes)
{
    analisador_t a;
    e->qtd_nos = 0;
    e->qtd_classes = 0;
    e->qtd_inst = 0;
    e->inicio = e->final = -1;
    e->erro = 0;
    e->erro_pos = 0;

    a.e = e;
    a.p = padrao;
    a.base = padrao;
    a.opcoes = opcoes;
    a.prof = 0;
    a.erro = 0;
    a.erro_pos = 0;

    int raiz = ler_alternativa(&a);
    if (raiz >= 0 && *a.p) falha(&a, "símbolo inesperado");
    if (raiz < 0) {
        e->erro = a.erro ? a.erro : "expressão inválida";
        e->erro_pos = a.erro_pos;
        return 0;
    }

    if (opcoes & EXP_PALAVRA_INTEIRA) {
        int i = novo_no(&a, EXP_NO_INI_OK);
        int f = novo_no(&a, EXP_NO_FIM_OK);
        if (i < 0 || f < 0) goto erro;
        raiz = concatenar(&a, i, raiz);
        if (raiz < 0) goto erro;
        raiz = concatenar(&a, raiz, f);
        if (raiz < 0) goto erro;
    }
    if (opcoes & EXP_LINHA_INTEIRA) {
        int b = novo_no(&a, EXP_NO_BOL);
        int f = novo_no(&a, EXP_NO_EOL);
        if (b < 0 || f < 0) goto erro;
        raiz = concatenar(&a, b, raiz);
        if (raiz < 0) goto erro;
        raiz = concatenar(&a, raiz, f);
        if (raiz < 0) goto erro;
    }

    {
        compilador_t c;
        c.e = e;
        c.qtd_pend = 0;
        c.erro = 0;
        fragmento_t fr = compilar_no(&c, raiz);
        if (!c.erro) {
            int fim = emitir(&c, EXP_I_FIM);
            if (fim < 0) c.erro = 1;
            else {
                ligar_lista(&c, fr.head, fim);
                e->inicio = fr.ini;
                e->final = fim;
            }
        }
        if (c.erro) {
            e->erro = "expressão grande demais";
            e->erro_pos = 0;
            return 0;
        }
    }
    return 1;

erro:
    e->erro = a.erro ? a.erro : "expressão grande demais";
    e->erro_pos = a.erro_pos;
    return 0;
}

const char *expressao_erro(const expressao_t *e)
{
    return e->erro;
}

/* ------------------------------------------------------------------ busca */

typedef struct { short pc; int ini; } thread_t;

static int inst_casa(const expressao_t *e, int pc, int c)
{
    const exp_inst_t *x = &e->inst[pc];
    switch (x->tipo) {
    case EXP_I_CAR:     return (u8)c == x->classe;
    case EXP_I_CAR_CI:  return minuscula((u8)c) == minuscula(x->classe);
    case EXP_I_CLASSE:  return set_tem(e->classes[x->classe].bits, (u8)c);
    case EXP_I_PONTO:   return (u8)c != '\n';
    default:            return 0;
    }
}

/* Expande as transições vazias a partir de "pc" na posição pos.  O "ini" é
 * o começo do trecho (para as asserções do -w).  "visto" vale para a
 * posição inteira, então cada estado entra uma vez só. */
static void adicionar(const expressao_t *e, const char *t, int n, int pos,
                      int pc, int ini, u8 *visto, thread_t *lista, int *qtd,
                      short *pilha, int *qp)
{
    *qp = 0;
    pilha[(*qp)++] = (short)pc;
    while (*qp > 0) {
        int s = pilha[--(*qp)];
        if (s < 0) continue;
        if (visto[s >> 3] & (1u << (s & 7))) continue;
        visto[s >> 3] |= (u8)(1u << (s & 7));
        const exp_inst_t *x = &e->inst[s];
        int frente = pos > 0 && eh_palavra((u8)t[pos - 1]);
        int tras   = pos < n && eh_palavra((u8)t[pos]);
        switch (x->tipo) {
        case EXP_I_DIVIDE:
            if (*qp + 2 <= EXP_MAX_PILHA) {
                pilha[(*qp)++] = x->saida2;
                pilha[(*qp)++] = x->saida1;
            }
            break;
        case EXP_I_NOP:
            if (*qp < EXP_MAX_PILHA) pilha[(*qp)++] = x->saida1;
            break;
        case EXP_I_BOL:
            if (pos == 0 && *qp < EXP_MAX_PILHA) pilha[(*qp)++] = x->saida1;
            break;
        case EXP_I_EOL:
            if (pos == n && *qp < EXP_MAX_PILHA) pilha[(*qp)++] = x->saida1;
            break;
        case EXP_I_PALAVRA:
            if ((frente != tras) && *qp < EXP_MAX_PILHA) pilha[(*qp)++] = x->saida1;
            break;
        case EXP_I_NAO_PALAVRA:
            if ((frente == tras) && *qp < EXP_MAX_PILHA) pilha[(*qp)++] = x->saida1;
            break;
        case EXP_I_INI_PALAVRA:
            if (tras && !frente && *qp < EXP_MAX_PILHA) pilha[(*qp)++] = x->saida1;
            break;
        case EXP_I_FIM_PALAVRA:
            if (frente && !tras && *qp < EXP_MAX_PILHA) pilha[(*qp)++] = x->saida1;
            break;
        case EXP_I_INI_OK:
            if ((ini == 0 || !eh_palavra((u8)t[ini - 1])) && *qp < EXP_MAX_PILHA)
                pilha[(*qp)++] = x->saida1;
            break;
        case EXP_I_FIM_OK:
            if ((pos == n || !eh_palavra((u8)t[pos])) && *qp < EXP_MAX_PILHA)
                pilha[(*qp)++] = x->saida1;
            break;
        default:
            if (*qtd < EXP_MAX_ESTADOS) {
                thread_t th;
                th.pc = (short)s;
                th.ini = ini;
                lista[(*qtd)++] = th;
            }
            break;
        }
    }
}

int expressao_buscar(const expressao_t *e, const char *t, int n, int inicio,
                     int *ini, int *fim)
{
    thread_t atual[EXP_MAX_ESTADOS], prox[EXP_MAX_ESTADOS];
    u8 visto[EXP_MAX_ESTADOS / 8], visto2[EXP_MAX_ESTADOS / 8];
    short pilha[EXP_MAX_PILHA];
    int qa = 0, qp = 0, qpilha = 0;
    int melhor_ini = -1, melhor_fim = -1;

    if (!e || e->inicio < 0) return 0;
    if (inicio < 0) inicio = 0;
    if (inicio > n) return 0;

    int pos = inicio;
    zerar(visto, (int)sizeof visto);
    adicionar(e, t, n, pos, e->inicio, pos, visto, atual, &qa, pilha, &qpilha);

    for (;;) {
        zerar(visto2, (int)sizeof visto2);
        qp = 0;
        for (int i = 0; i < qa; i++) {
            int pc = atual[i].pc, ini_t = atual[i].ini;
            if (e->inst[pc].tipo == EXP_I_FIM) {
                if (melhor_ini < 0 || ini_t < melhor_ini ||
                    (ini_t == melhor_ini && pos > melhor_fim)) {
                    melhor_ini = ini_t;
                    melhor_fim = pos;
                }
            } else if (pos < n && inst_casa(e, pc, (u8)t[pos])) {
                adicionar(e, t, n, pos + 1, e->inst[pc].saida1, ini_t, visto2,
                          prox, &qp, pilha, &qpilha);
            }
        }
        if (pos >= n) break;
        if (melhor_ini < 0 || pos + 1 <= melhor_ini)
            adicionar(e, t, n, pos + 1, e->inicio, pos + 1, visto2, prox, &qp,
                      pilha, &qpilha);
        copiar(atual, prox, qp * (int)sizeof(thread_t));
        qa = qp;
        if (melhor_ini >= 0) {
            int algum = 0;
            for (int i = 0; i < qa; i++)
                if (atual[i].ini <= melhor_ini) { algum = 1; break; }
            if (!algum) break;
        }
        pos++;
    }

    if (melhor_ini >= 0) {
        *ini = melhor_ini;
        *fim = melhor_fim;
        return 1;
    }
    return 0;
}

int expressao_casa(const expressao_t *e, const char *texto, int n)
{
    int ini, fim;
    return expressao_buscar(e, texto, n, 0, &ini, &fim);
}
