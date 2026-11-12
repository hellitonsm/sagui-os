/* grep (procurar) - procura padrões em arquivos e na entrada (H50).
 *
 * É o grep do Linux para o SaguiOS: expressões regulares BRE (padrão) e ERE
 * (-E), texto fixo (-F), várias opções e a mesma saída (prefixos, contexto,
 * cores, códigos de saída).  O motor de expressões está em
 * libsagui/expressao.c; aqui ficam as opções, a leitura dos arquivos, as
 * linhas, o contexto, o -o, o -F e a recursão em pastas.
 *
 * O texto é lido inteiro para a memória (até 32 MiB), convertido de UTF-8
 * para Latin-1 como o "cat" faz (D027) e separado em linhas.  Isso deixa a
 * detecção de arquivo binário e o -c iguais aos do grep: um NUL vira fim de
 * linha quando o arquivo é binário, e a saída é "Binary file ... matches".
 *
 * O que não existe aqui: -P (PCRE), referências anteriores (\1) e -z.  O
 * resto das opções do grep do Linux está na ajuda (--help). */
#include "sagui.h"
#include "expressao.h"

#define MAX_PADROES        64
#define MAX_INCLUDE        16
#define MAX_EXCLUDE        64
#define MAX_EXCLUDE_DIR    16
#define MAX_OPERANDOS      128
#define LIMITE_ARQUIVO     (32u * 1024u * 1024u)
#define LIMITE_PADROES     (1u * 1024u * 1024u)
#define MAX_PROFUNDIDADE   24
#define TAM_CAMINHO        512
#define TAM_BUF_SAIDA      4096

#define COR_MATCH "\x1b[01;31m\x1b[K"
#define COR_NOME  "\x1b[35m\x1b[K"
#define COR_SEP   "\x1b[36m\x1b[K"
#define COR_NUM   "\x1b[32m\x1b[K"
#define COR_FIM   "\x1b[m\x1b[K"

enum { MODO_BRE = 0, MODO_ERE = 1, MODO_FIXO = 2 };
enum { DIR_LER = 0, DIR_PULAR = 1, DIR_RECURSIVO = 2 };
enum { LISTAR_NENHUM = 0, LISTAR_COM = 1, LISTAR_SEM = 2 };
enum { BINARIO_PADRAO = 0, BINARIO_TEXTO = 1, BINARIO_SEM = 2 };

typedef struct {
    int  modo;
    int  caso_insensivel;
    int  inverter;
    int  palavra;
    int  linha_inteira;
    int  num_linha;
    int  desloc_byte;
    int  contar;
    int  somente;
    int  silencioso;
    int  sem_mensagens;
    int  listar_modo;
    int  recursivo;
    int  dir_modo;
    int  binario;
    int  forca_nome;      /* 0 automático, 1 -H, 2 -h */
    int  nulo;            /* -Z */
    int  antes, depois;
    int  contexto_pedido;
    int  max_cont;        /* -1 = sem limite */
    int  cor;             /* 1 = --color=always */
    int  tab;             /* -T: um tab depois dos prefixos */
    int  grupo_sep;
    const char *sep_grupo;
    const char *rotulo_stdin;
} opcoes_t;

typedef struct {
    const char *dados;
    unsigned    tam;
    const char *nome_exib;
    int         mostrar_nome;
    int         num;
    int         ultima_impressa;
    int         faltam_depois;
    int         selecionadas;
    int         atingiu_m;
    int         parar;
    int         binario;
} estado_t;

typedef struct {
    unsigned ini, tam;
    int      num;
} linha_ctx_t;

static opcoes_t o;
static char *padroes[MAX_PADROES];
static expressao_t *res[MAX_PADROES];
static int npad;

static const char *include[MAX_INCLUDE];
static int n_include;
static const char *exclude[MAX_EXCLUDE];
static int n_exclude;
static const char *exclude_dir[MAX_EXCLUDE_DIR];
static int n_exclude_dir;

static const char *operandos[MAX_OPERANDOS];
static int n_operandos;

static char cam_real[MAX_PROFUNDIDADE][TAM_CAMINHO];
static char cam_exib[MAX_PROFUNDIDADE][TAM_CAMINHO];

static char sbuf[TAM_BUF_SAIDA];
static unsigned s_n;

static estado_t st;
static linha_ctx_t *anel;
static int anel_cap, anel_pos, anel_qtd;

static int achou_algum;
static int houve_erro;
static int parar_tudo;
static int ja_impresso;

/* --------------------------------------------------------------- saída */

static void s_flush(void)
{
    if (s_n) { escrever_tudo(SAIDA, sbuf, s_n); s_n = 0; }
}

static void s_bytes(const void *p, unsigned n)
{
    const char *c = (const char *)p;
    while (n > 0) {
        unsigned k = TAM_BUF_SAIDA - s_n;
        if (k > n) k = n;
        memcpy(sbuf + s_n, c, k);
        s_n += k;
        c += k;
        n -= k;
        if (s_n == TAM_BUF_SAIDA) s_flush();
    }
}

static void s_text(const char *t)
{
    if (t) s_bytes(t, (unsigned)tam_texto(t));
}

static void s_char(char c)
{
    s_bytes(&c, 1);
}

static void s_int(int v)
{
    char t[16];
    formatar_texto(t, sizeof t, "%d", v);
    s_text(t);
}

/* --------------------------------------------------------------- letras */

static int minuscula(int c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
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

/* --------------------------------------------------------------- glob */

/* Casa um nome (sem pasta) com um padrão de --include/--exclude: * ? [..].
 * É o suficiente para os padrões usuais do grep (fnmatch). */
static int glob_casa(const char *p, const char *s)
{
    while (*p) {
        if (*p == '*') {
            p++;
            while (*p == '*') p++;
            if (!*p) return 1;
            for (;;) {
                if (glob_casa(p, s)) return 1;
                if (!*s) return 0;
                s++;
            }
        } else if (*p == '?') {
            if (!*s) return 0;
            p++; s++;
        } else if (*p == '[') {
            int neg = 0, ok = 0, primeiro = 1;
            p++;
            if (*p == '!' || *p == '^') { neg = 1; p++; }
            while (*p && (*p != ']' || primeiro)) {
                int a, b;
                primeiro = 0;
                if (*p == '\\' && p[1]) p++;
                a = (unsigned char)*p;
                if (p[1] == '-' && p[2] && p[2] != ']') {
                    p += 2;
                    if (*p == '\\' && p[1]) p++;
                    b = (unsigned char)*p;
                    if ((unsigned char)*s >= a && (unsigned char)*s <= b) ok = 1;
                    p++;
                } else {
                    if ((unsigned char)*s == a) ok = 1;
                    p++;
                }
            }
            if (*p == ']') p++;
            if (!*s) return 0;
            if (ok == neg) return 0;
            s++;
        } else if (*p == '\\' && p[1]) {
            p++;
            if (*p != *s) return 0;
            p++; s++;
        } else {
            if (*p != *s) return 0;
            p++; s++;
        }
    }
    return *s == 0;
}

static const char *nome_base(const char *caminho)
{
    const char *p = acha_ultimo_char(caminho, '/');
    return p ? p + 1 : caminho;
}

static int passa_exclude_dir(const char *nome)
{
    const char *b = nome_base(nome);
    for (int i = 0; i < n_exclude_dir; i++)
        if (glob_casa(exclude_dir[i], b)) return 0;
    return 1;
}

/* Como o grep: um padrão com "/" casa com o caminho todo (o nome que sai na
 * saída); sem "/", casa só com o nome do arquivo. */
static int casa_filtro(const char *pat, const char *caminho)
{
    if (acha_char(pat, '/')) return glob_casa(pat, caminho);
    return glob_casa(pat, nome_base(caminho));
}

static int passa_filtros(const char *caminho)
{
    int i;
    if (n_include > 0) {
        int achou = 0;
        for (i = 0; i < n_include; i++)
            if (casa_filtro(include[i], caminho)) { achou = 1; break; }
        if (!achou) return 0;
    }
    for (i = 0; i < n_exclude; i++)
        if (casa_filtro(exclude[i], caminho)) return 0;
    return 1;
}

/* --------------------------------------------------------------- texto fixo */

static int iguais_ci(const char *a, const char *b, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        int x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x == y) continue;
        if (!o.caso_insensivel) return 0;
        if (minuscula(x) != minuscula(y)) return 0;
    }
    return 1;
}

/* Procura o literal "pat" (tamanho m) em "linha" a partir de "pos".
 * Devolve 1 e o trecho; com -x, só a linha inteira; com -w, só palavra. */
static int literal_buscar(const char *pat, unsigned m, const char *linha,
                          unsigned n, unsigned pos, unsigned *pini, unsigned *pfim)
{
    if (o.linha_inteira) {
        if (n == m && iguais_ci(pat, linha, m)) {
            *pini = 0;
            *pfim = n;
            return 1;
        }
        return 0;
    }
    if (m == 0) {
        /* Com -w, o vazio só casa onde não há letra dos dois lados. */
        unsigned i = pos;
        for (;;) {
            int antes_ok = !o.palavra || i == 0 ||
                           !eh_palavra((unsigned char)linha[i - 1]);
            int depois_ok = !o.palavra || i >= n ||
                            !eh_palavra((unsigned char)linha[i]);
            if (antes_ok && depois_ok) {
                *pini = i;
                *pfim = i;
                return 1;
            }
            if (i >= n) return 0;
            i++;
        }
    }
    for (unsigned i = pos; i + m <= n; i++) {
        if (!iguais_ci(pat, linha + i, m)) continue;
        if (o.palavra) {
            if (i > 0 && eh_palavra((unsigned char)linha[i - 1])) continue;
            if (i + m < n && eh_palavra((unsigned char)linha[i + m])) continue;
        }
        *pini = i;
        *pfim = i + m;
        return 1;
    }
    return 0;
}

/* --------------------------------------------------------------- motor */

/* Próximo trecho que casa, escolhendo entre todos os padrões o mais à
 * esquerda e, no mesmo ponto, o mais longo (como o grep -o do Linux). */
static int proxima_correspondencia(const char *linha, unsigned n, unsigned pos,
                                   unsigned *pini, unsigned *pfim)
{
    int achou = 0;
    unsigned mi = 0, mf = 0;

    for (int k = 0; k < npad; k++) {
        unsigned i = 0, f = 0;
        int r;
        if (o.modo == MODO_FIXO) {
            r = literal_buscar(padroes[k], (unsigned)tam_texto(padroes[k]),
                               linha, n, pos, &i, &f);
        } else {
            int ii = 0, ff = 0;
            r = expressao_buscar(res[k], linha, (int)n, (int)pos, &ii, &ff);
            if (r) { i = (unsigned)ii; f = (unsigned)ff; }
        }
        if (r && (!achou || i < mi || (i == mi && f > mf))) {
            achou = 1;
            mi = i;
            mf = f;
        }
    }
    if (achou) { *pini = mi; *pfim = mf; }
    return achou;
}

static int linha_selecionada(const char *linha, unsigned n)
{
    int casa = 0;
    if (o.modo == MODO_FIXO) {
        for (int k = 0; k < npad; k++) {
            unsigned i, f;
            if (literal_buscar(padroes[k], (unsigned)tam_texto(padroes[k]),
                               linha, n, 0, &i, &f)) { casa = 1; break; }
        }
    } else {
        for (int k = 0; k < npad; k++)
            if (expressao_casa(res[k], linha, (int)n)) { casa = 1; break; }
    }
    return o.inverter ? !casa : casa;
}

/* --------------------------------------------------------------- prefixos */

static void prefixo(const char *nome, int mostrar_nome, int num, int mostrar_num,
                    unsigned desloc, int mostrar_desloc, char sep)
{
    if (mostrar_nome && nome) {
        if (o.cor) s_text(COR_NOME);
        s_text(nome);
        if (o.cor) s_text(COR_FIM);
        if (o.nulo) {
            s_char('\0');
        } else {
            if (o.cor) s_text(COR_SEP);
            s_char(sep);
            if (o.cor) s_text(COR_FIM);
        }
    }
    if (mostrar_num) {
        if (o.cor) s_text(COR_NUM);
        s_int(num);
        if (o.cor) s_text(COR_FIM);
        if (o.cor) s_text(COR_SEP);
        s_char(sep);
        if (o.cor) s_text(COR_FIM);
    }
    if (mostrar_desloc) {
        if (o.cor) s_text(COR_NUM);
        s_int((int)desloc);
        if (o.cor) s_text(COR_FIM);
        if (o.cor) s_text(COR_SEP);
        s_char(sep);
        if (o.cor) s_text(COR_FIM);
    }
}

static void separador_grupo(void)
{
    if (!o.grupo_sep) return;
    if (o.cor) s_text(COR_SEP);
    s_text(o.sep_grupo);
    if (o.cor) s_text(COR_FIM);
    s_char('\n');
}

/* -T: o texto começa num ponto de tabulação depois dos prefixos. */
static void tab_inicial(void)
{
    if (o.tab && (o.num_linha || o.desloc_byte || st.mostrar_nome))
        s_char('\t');
}

static void texto_colorido(const char *linha, unsigned n)
{
    unsigned busca = 0, saida = 0, i, f;
    while (busca <= n && proxima_correspondencia(linha, n, busca, &i, &f)) {
        if (f > i) {
            if (i > saida) s_bytes(linha + saida, i - saida);
            s_text(COR_MATCH);
            s_bytes(linha + i, f - i);
            s_text(COR_FIM);
            saida = f;
            busca = f;
        } else {
            if (i >= n) break;
            busca = i + 1;
        }
    }
    if (n > saida) s_bytes(linha + saida, n - saida);
}

static void imprimir_linha(int num, const char *linha, unsigned tam,
                           unsigned desloc, int contexto)
{
    if (o.contexto_pedido) {
        if (st.ultima_impressa == 0) {
            if (ja_impresso) separador_grupo();
        } else if (num != st.ultima_impressa + 1) {
            separador_grupo();
        }
    }
    prefixo(st.nome_exib, st.mostrar_nome, num, o.num_linha,
            desloc, o.desloc_byte, contexto ? '-' : ':');
    tab_inicial();
    if (!contexto && o.cor && !o.inverter) texto_colorido(linha, tam);
    else s_bytes(linha, tam);
    s_char('\n');
    st.ultima_impressa = num;
    ja_impresso = 1;
}

static void imprimir_nome_lista(const char *nome)
{
    if (o.cor) s_text(COR_NOME);
    s_text(nome);
    if (o.cor) s_text(COR_FIM);
    if (o.nulo) s_char('\0');
    else s_char('\n');
}

static void imprimir_somente(const char *linha, unsigned tam, unsigned desloc, int num)
{
    unsigned busca = 0, i, f;
    while (busca <= tam && proxima_correspondencia(linha, tam, busca, &i, &f)) {
        if (f > i) {
            prefixo(st.nome_exib, st.mostrar_nome, num, o.num_linha,
                    desloc + i, o.desloc_byte, ':');
            tab_inicial();
            if (o.cor) {
                s_text(COR_MATCH);
                s_bytes(linha + i, f - i);
                s_text(COR_FIM);
            } else {
                s_bytes(linha + i, f - i);
            }
            s_char('\n');
            busca = f;
        } else {
            if (i >= tam) break;
            busca = i + 1;
        }
    }
}

static void imprimir_contagem(void)
{
    prefixo(st.nome_exib, st.mostrar_nome, 0, 0, 0, 0, ':');
    s_int(st.selecionadas);
    s_char('\n');
}

static void mensagem_binaria(void)
{
    s_text("Binary file ");
    s_text(st.nome_exib);
    s_text(" matches\n");
}

/* --------------------------------------------------------------- contexto */

static void anel_iniciar(void)
{
    anel = NULL;
    anel_cap = o.antes;
    anel_pos = 0;
    anel_qtd = 0;
    if (anel_cap > 0) {
        anel = (linha_ctx_t *)aloca((size_t)anel_cap * sizeof(linha_ctx_t));
        if (!anel) anel_cap = 0;
    }
}

static void anel_encerrar(void)
{
    if (anel) { libera(anel); anel = NULL; }
    anel_cap = 0;
}

static void anel_juntar(unsigned ini, unsigned tam, int num)
{
    if (anel_cap <= 0) return;
    anel[anel_pos].ini = ini;
    anel[anel_pos].tam = tam;
    anel[anel_pos].num = num;
    anel_pos = (anel_pos + 1) % anel_cap;
    if (anel_qtd < anel_cap) anel_qtd++;
}

static void anel_despejar(int num_atual)
{
    if (anel_cap <= 0) return;
    for (int k = 0; k < anel_qtd; k++) {
        int p = (anel_pos - anel_qtd + k + anel_cap) % anel_cap;
        linha_ctx_t *e = &anel[p];
        if (e->num < num_atual && e->num > st.ultima_impressa)
            imprimir_linha(e->num, st.dados + e->ini, e->tam, e->ini, 1);
    }
}

/* --------------------------------------------------------------- linhas */

static void processar_linha(const char *linha, unsigned tam, unsigned desloc)
{
    int num = st.num;
    int selecionada = 0;

    if (!st.atingiu_m)
        selecionada = linha_selecionada(linha, tam);

    if (selecionada) {
        st.selecionadas++;
        achou_algum = 1;

        if (o.silencioso) {
            parar_tudo = 1;
            st.parar = 1;
            return;
        }
        if (o.listar_modo == LISTAR_COM) {
            imprimir_nome_lista(st.nome_exib);
            st.parar = 1;
            return;
        }
        if (o.listar_modo == LISTAR_SEM) {
            st.parar = 1;
            return;
        }
        if (st.binario) {
            if (!o.contar) {
                mensagem_binaria();
                st.parar = 1;
                return;
            }
        } else if (o.contar) {
            /* só conta */
        } else if (o.somente) {
            imprimir_somente(linha, tam, desloc, num);
        } else {
            anel_despejar(num);
            imprimir_linha(num, linha, tam, desloc, 0);
            st.faltam_depois = o.depois;
        }

        if (o.max_cont >= 0 && st.selecionadas >= o.max_cont) {
            st.atingiu_m = 1;
            if (o.depois <= 0 || o.contar || o.somente ||
                o.listar_modo != LISTAR_NENHUM)
                st.parar = 1;
        }
    } else {
        if (!st.binario && !o.contar && !o.somente &&
            o.listar_modo == LISTAR_NENHUM && !o.silencioso) {
            if (st.faltam_depois > 0) {
                imprimir_linha(num, linha, tam, desloc, 1);
                st.faltam_depois--;
                if (st.atingiu_m && st.faltam_depois == 0) st.parar = 1;
            } else if (st.atingiu_m) {
                st.parar = 1;
            }
        } else if (st.atingiu_m) {
            st.parar = 1;
        }
    }

    if (!st.parar && o.contexto_pedido && !st.binario && !o.contar &&
        !o.somente && o.listar_modo == LISTAR_NENHUM && !o.silencioso)
        anel_juntar(desloc, tam, num);

    st.num++;
}

static void processar_dados(const char *nome, const char *dados, unsigned n, int binario)
{
    st.dados = dados;
    st.tam = n;
    st.nome_exib = nome;
    st.num = 1;
    st.ultima_impressa = 0;
    st.faltam_depois = 0;
    st.selecionadas = 0;
    st.atingiu_m = 0;
    st.parar = 0;
    st.binario = binario;

    if (binario && o.binario == BINARIO_SEM) {
        if (o.listar_modo == LISTAR_SEM) imprimir_nome_lista(nome);
        else if (o.contar) imprimir_contagem();
        return;
    }

    anel_iniciar();
    if (n > 0) {
        unsigned ini = 0;
        for (unsigned i = 0; i < n && !st.parar; i++) {
            if (dados[i] == '\n') {
                processar_linha(dados + ini, i - ini, ini);
                ini = i + 1;
            }
        }
        if (!st.parar && ini < n)
            processar_linha(dados + ini, n - ini, ini);
    }
    anel_encerrar();

    if (o.listar_modo == LISTAR_SEM && !st.parar)
        imprimir_nome_lista(nome);
    else if (o.contar)
        imprimir_contagem();
}

/* --------------------------------------------------------------- arquivos */

static void erro_arquivo(const char *nome, int erro)
{
    houve_erro = 1;
    if (!o.sem_mensagens)
        imprimir_em(ERROS, "grep: %s: %s\n", nome, nome_erro(erro));
}

/* Lê tudo de fd para um bloco novo.  Devolve 0 e preenche; -1 sem memória,
 * -2 erro de leitura, -3 maior que o limite. */
static int ler_tudo(int fd, char **pbuf, unsigned *pn)
{
    unsigned cap = 64 * 1024, n = 0;
    char *b = (char *)aloca(cap);
    if (!b) return -1;
    for (;;) {
        if (n == cap) {
            unsigned nova;
            char *nb;
            if (cap >= LIMITE_ARQUIVO) { libera(b); return -3; }
            nova = cap * 2;
            if (nova > LIMITE_ARQUIVO) nova = LIMITE_ARQUIVO;
            nb = (char *)realoca(b, nova);
            if (!nb) { libera(b); return -1; }
            b = nb;
            cap = nova;
        }
        int r = ler(fd, b + n, cap - n);
        if (r < 0) { libera(b); return -2; }
        if (r == 0) break;
        n += (unsigned)r;
    }
    *pbuf = b;
    *pn = n;
    return 0;
}

static void processar_arquivo(const char *real, const char *exib, int mostrar_nome)
{
    int fd = ENTRADA;
    char *b = NULL;
    unsigned n = 0;
    int r;

    if (compara_texto(real, "-") != 0) {
        fd = abrir(real, ABRIR_LER);
        if (fd < 0) { erro_arquivo(exib, fd); return; }
    }
    r = ler_tudo(fd, &b, &n);
    if (fd != ENTRADA) fechar(fd);
    if (r == -1) { erro_arquivo(exib, ERRO_SEM_MEMORIA); return; }
    if (r == -2) { erro_arquivo(exib, ERRO_INVALIDO); return; }
    if (r == -3) {
        houve_erro = 1;
        if (!o.sem_mensagens)
            imprimir_em(ERROS, "grep: %s: arquivo grande demais\n", exib);
        return;
    }

    /* Guarda se era binário antes de converter (o NUL é o sinal). */
    int binario = 0;
    for (unsigned i = 0; i < n; i++)
        if (b[i] == 0) { binario = 1; break; }

    /* Os arquivos do sistema são UTF-8; a tela e o motor são Latin-1 (D027). */
    if (n > 0) n = (unsigned)utf8_para_latin1(b, n, NULL);

    if (binario && o.binario == BINARIO_PADRAO) {
        /* O grep trata NUL como fim de linha no modo binário. */
        for (unsigned i = 0; i < n; i++)
            if (b[i] == 0) b[i] = '\n';
    }
    if (binario && o.binario == BINARIO_TEXTO) binario = 0;

    st.mostrar_nome = mostrar_nome;
    processar_dados(exib, b, n, binario);
    libera(b);
}

/* Junta base + "/" + nome (sem barra dupla). */
static void juntar_caminho(char *dst, const char *base, const char *nome, unsigned max)
{
    unsigned n = 0;
    if (base && base[0]) {
        while (base[n] && n + 1 < max) { dst[n] = base[n]; n++; }
        if (n > 0 && dst[n - 1] != '/' && n + 1 < max) dst[n++] = '/';
    }
    unsigned i = 0;
    while (nome[i] && n + 1 < max) { dst[n++] = nome[i]; i++; }
    dst[n] = 0;
}

static void percorrer(int nivel, const char *dir_real, const char *dir_exib)
{
    entrada_pasta_t e;
    int i = 0, r;

    if (nivel + 1 >= MAX_PROFUNDIDADE) {
        houve_erro = 1;
        if (!o.sem_mensagens)
            imprimir_em(ERROS, "grep: %s: pastas fundas demais\n", dir_exib);
        return;
    }

    while (!parar_tudo && (r = listar(dir_real, i, &e)) == 1) {
        char *cr = cam_real[nivel + 1];
        char *ce = cam_exib[nivel + 1];

        if (compara_texto(e.nome, ".") == 0 || compara_texto(e.nome, "..") == 0) {
            i++;
            continue;
        }
        juntar_caminho(cr, dir_real, e.nome, TAM_CAMINHO);
        juntar_caminho(ce, dir_exib, e.nome, TAM_CAMINHO);

        if (e.tipo == TIPO_PASTA) {
            if (passa_exclude_dir(e.nome))
                percorrer(nivel + 1, cr, ce);
        } else if (passa_filtros(ce)) {
            if (o.max_cont == 0)
                imprimir_nome_lista(ce);   /* -m0 -L: não lê o conteúdo */
            else
                processar_arquivo(cr, ce, o.forca_nome != 2);
        }
        i++;
    }
    if (r < 0) erro_arquivo(dir_exib, r);
}

static int mostrar_para_operando(void)
{
    if (o.forca_nome == 1) return 1;
    if (o.forca_nome == 2) return 0;
    if (n_operandos > 1) return 1;
    return 0;
}

static void processar_operando(const char *real, const char *exib)
{
    info_arquivo_t inf;
    int r;

    if (compara_texto(real, "-") == 0) {
        processar_arquivo("-", exib, mostrar_para_operando());
        return;
    }
    r = info_arquivo(real, &inf);
    if (r < 0) { erro_arquivo(exib, r); return; }

    if (inf.tipo == TIPO_PASTA) {
        if (!passa_exclude_dir(exib)) return;
        if (o.dir_modo == DIR_RECURSIVO)
            percorrer(0, real, exib);
        else if (o.dir_modo != DIR_PULAR)
            erro_arquivo(exib, ERRO_E_PASTA);
        return;
    }
    if (passa_filtros(exib))
        processar_arquivo(real, exib, mostrar_para_operando());
}

/* Com -m0 e -L, o grep lista os arquivos que existem sem ler o conteúdo. */
static void listar_sem_ler(const char *real, const char *exib)
{
    info_arquivo_t inf;
    int r;
    if (compara_texto(real, "-") == 0) {
        imprimir_nome_lista(exib);
        return;
    }
    r = info_arquivo(real, &inf);
    if (r < 0) { erro_arquivo(exib, r); return; }
    if (inf.tipo == TIPO_PASTA) {
        if (!passa_exclude_dir(exib)) return;
        if (o.dir_modo == DIR_RECURSIVO)
            percorrer(0, real, exib);
        else if (o.dir_modo != DIR_PULAR)
            erro_arquivo(exib, ERRO_E_PASTA);
        return;
    }
    if (passa_filtros(exib)) imprimir_nome_lista(exib);
}

/* --------------------------------------------------------------- opções */

static int adicionar_padrao_n(const char *s, unsigned n)
{
    if (npad >= MAX_PADROES) {
        imprimir_em(ERROS, "grep: padrões demais (máximo %d)\n", MAX_PADROES);
        return -1;
    }
    char *p = (char *)aloca(n + 1);
    if (!p) {
        imprimir_em(ERROS, "grep: sem memória\n");
        return -1;
    }
    if (n) memcpy(p, s, n);
    p[n] = 0;
    padroes[npad++] = p;
    return 0;
}

static int adicionar_padrao(const char *s)
{
    return adicionar_padrao_n(s, (unsigned)tam_texto(s));
}

static int ler_padroes_de(const char *nome)
{
    int fd = ENTRADA;
    char *b = NULL;
    unsigned n = 0;
    int r;

    if (compara_texto(nome, "-") != 0) {
        fd = abrir(nome, ABRIR_LER);
        if (fd < 0) {
            houve_erro = 1;
            if (!o.sem_mensagens)
                imprimir_em(ERROS, "grep: %s: %s\n", nome, nome_erro(fd));
            return -1;
        }
    }
    r = ler_tudo(fd, &b, &n);
    if (fd != ENTRADA) fechar(fd);
    if (r == -1) {
        imprimir_em(ERROS, "grep: %s: sem memória\n", nome);
        return -1;
    }
    if (r == -2) {
        imprimir_em(ERROS, "grep: %s: erro de leitura\n", nome);
        return -1;
    }
    if (r == -3) {
        imprimir_em(ERROS, "grep: %s: arquivo de padrões grande demais\n", nome);
        return -1;
    }

    if (n > 0) n = (unsigned)utf8_para_latin1(b, n, NULL);

    unsigned ini = 0;
    for (unsigned i = 0; i < n; i++) {
        if (b[i] == '\n') {
            if (adicionar_padrao_n(b + ini, i - ini) < 0) { libera(b); return -1; }
            ini = i + 1;
        }
    }
    if (ini < n) {
        if (adicionar_padrao_n(b + ini, n - ini) < 0) { libera(b); return -1; }
    }
    libera(b);
    return 0;
}

/* --exclude-from=ARQUIVO: cada linha é um padrão de --exclude. */
static int adicionar_exclude_n(const char *s, unsigned n)
{
    if (n_exclude >= MAX_EXCLUDE) {
        imprimir_em(ERROS, "grep: exclusões demais (máximo %d)\n", MAX_EXCLUDE);
        return -1;
    }
    char *p = (char *)aloca(n + 1);
    if (!p) {
        imprimir_em(ERROS, "grep: sem memória\n");
        return -1;
    }
    if (n) memcpy(p, s, n);
    p[n] = 0;
    exclude[n_exclude++] = p;
    return 0;
}

static int ler_excludes_de(const char *nome)
{
    int fd = ENTRADA;
    char *b = NULL;
    unsigned n = 0;
    int r;

    if (compara_texto(nome, "-") != 0) {
        fd = abrir(nome, ABRIR_LER);
        if (fd < 0) {
            houve_erro = 1;
            if (!o.sem_mensagens)
                imprimir_em(ERROS, "grep: %s: %s\n", nome, nome_erro(fd));
            return -1;
        }
    }
    r = ler_tudo(fd, &b, &n);
    if (fd != ENTRADA) fechar(fd);
    if (r == -1) {
        imprimir_em(ERROS, "grep: %s: sem memória\n", nome);
        return -1;
    }
    if (r == -2) {
        imprimir_em(ERROS, "grep: %s: erro de leitura\n", nome);
        return -1;
    }
    if (r == -3) {
        imprimir_em(ERROS, "grep: %s: arquivo grande demais\n", nome);
        return -1;
    }

    if (n > 0) n = (unsigned)utf8_para_latin1(b, n, NULL);

    unsigned ini = 0;
    for (unsigned i = 0; i < n; i++) {
        if (b[i] == '\n') {
            if (i > ini && adicionar_exclude_n(b + ini, i - ini) < 0) {
                libera(b);
                return -1;
            }
            ini = i + 1;
        }
    }
    if (ini < n) {
        if (adicionar_exclude_n(b + ini, n - ini) < 0) { libera(b); return -1; }
    }
    libera(b);
    return 0;
}

static int numero(const char *s, int *saida)
{
    int v = 0;
    if (!s || !*s) return 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') return 0;
        v = v * 10 + (*p - '0');
        if (v > 1000000) v = 1000000;
    }
    *saida = v;
    return 1;
}

/* Pega o argumento de uma opção curta que o exige (resto do grupo ou o
 * próximo argv).  Devolve NULL se faltou. */
static const char *arg_curto(char **argv, int argc, int *pi, char **pp)
{
    char *p = *pp;
    if (p[1]) {
        *pp = p + tam_texto(p) - 1;   /* para o laço parar no fim */
        return p + 1;
    }
    if (*pi + 1 >= argc) return NULL;
    (*pi)++;
    return argv[*pi];
}

static int opcao_longa(char **argv, int argc, int *pi)
{
    char *a = argv[*pi];
    char *nome = a + 2;
    char *val = NULL;
    char *igual = acha_char(nome, '=');
    char nomebuf[64];

    if (igual) {
        unsigned n = (unsigned)(igual - nome);
        if (n >= sizeof nomebuf) n = sizeof nomebuf - 1;
        memcpy(nomebuf, nome, n);
        nomebuf[n] = 0;
        nome = nomebuf;
        val = igual + 1;
    }

    if (compara_texto(nome, "help") == 0) {
        s_flush();
        imprimir(
            "uso: grep [opções] padrão [arquivo...]\n"
            "     grep [opções] -e padrão ... [arquivo...]\n"
            "\n"
            "Procura o padrão nas linhas dos arquivos (ou da entrada).\n"
            "Sem arquivo, lê a entrada; \"-\" também é a entrada.\n"
            "\n"
            "Escolha do padrão:\n"
            "  -E, --extended-regexp   expressões regulares estendidas (ERE)\n"
            "  -F, --fixed-strings     texto fixo, sem expressão regular\n"
            "  -G, --basic-regexp      expressões regulares básicas (padrão)\n"
            "  -e, --regexp=PADRÃO     usa PADRÃO (pode repetir)\n"
            "  -f, --file=ARQUIVO      lê os padrões de ARQUIVO (pode repetir)\n"
            "  -i, --ignore-case       não diferencia maiúsculas de minúsculas\n"
            "  -w, --word-regexp       só palavras inteiras\n"
            "  -x, --line-regexp       só linhas inteiras\n"
            "\n"
            "O que mostrar:\n"
            "  -v, --invert-match      as linhas que NÃO casam\n"
            "  -n, --line-number       número da linha\n"
            "  -b, --byte-offset       deslocamento em bytes\n"
            "  -o, --only-matching     só o trecho que casou\n"
            "  -c, --count             conta as linhas\n"
            "  -l, --files-with-matches  só os nomes dos arquivos com casamento\n"
            "  -L, --files-without-match só os nomes sem casamento\n"
            "  -q, --quiet, --silent   não escreve nada; só o código de saída\n"
            "  -s, --no-messages       não mostra erros de arquivo\n"
            "  -H, --with-filename     mostra o nome do arquivo\n"
            "  -h, --no-filename       não mostra o nome do arquivo\n"
            "  -Z, --null              termina o nome do arquivo com NUL\n"
            "  -T, --initial-tab       alinha o texto com um tab depois dos prefixos\n"
            "      --color[=QUANDO]    cores: always, never, auto (auto = never)\n"
            "\n"
            "Contexto:\n"
            "  -A, --after-context=N   N linhas depois\n"
            "  -B, --before-context=N  N linhas antes\n"
            "  -C, --context[=N]       N linhas antes e depois (padrão 2)\n"
            "  -NÚMERO                 o mesmo que -C NÚMERO\n"
            "      --no-group-separator  sem \"--\" entre grupos\n"
            "      --group-separator=SEP usa SEP entre grupos\n"
            "\n"
            "Arquivos e pastas:\n"
            "  -r, -R, --recursive     entra nas pastas\n"
            "      --include=GLOB      só arquivos que casam GLOB\n"
            "      --exclude=GLOB      pula arquivos que casam GLOB\n"
            "      --exclude-from=ARQ  lê os GLOB de exclusão de ARQ\n"
            "      --exclude-dir=GLOB  pula pastas que casam GLOB\n"
            "  -d, --directories=AÇÃO  read, skip ou recurse\n"
            "  -a, --text              trata arquivos binários como texto\n"
            "  -I                      pula arquivos binários\n"
            "      --binary-files=TIPO binary, text ou without-match\n"
            "  -m, --max-count=N       para depois de N linhas\n"
            "\n"
            "Não suportado: -P (PCRE), referências anteriores (\\1) e -z.\n"
            "Código de saída: 0 achou, 1 não achou, 2 erro.\n");
        return 1;
    }
    if (compara_texto(nome, "version") == 0) {
        s_flush();
        imprimir("grep (SaguiOS) 1.0 - H50\n");
        return 1;
    }
    if (compara_texto(nome, "extended-regexp") == 0) { o.modo = MODO_ERE; return 0; }
    if (compara_texto(nome, "fixed-strings") == 0) { o.modo = MODO_FIXO; return 0; }
    if (compara_texto(nome, "basic-regexp") == 0) { o.modo = MODO_BRE; return 0; }
    if (compara_texto(nome, "ignore-case") == 0) { o.caso_insensivel = 1; return 0; }
    if (compara_texto(nome, "invert-match") == 0) { o.inverter = 1; return 0; }
    if (compara_texto(nome, "word-regexp") == 0) { o.palavra = 1; return 0; }
    if (compara_texto(nome, "line-regexp") == 0) { o.linha_inteira = 1; return 0; }
    if (compara_texto(nome, "line-number") == 0) { o.num_linha = 1; return 0; }
    if (compara_texto(nome, "byte-offset") == 0) { o.desloc_byte = 1; return 0; }
    if (compara_texto(nome, "count") == 0) { o.contar = 1; return 0; }
    if (compara_texto(nome, "only-matching") == 0) { o.somente = 1; return 0; }
    if (compara_texto(nome, "quiet") == 0 ||
        compara_texto(nome, "silent") == 0) { o.silencioso = 1; return 0; }
    if (compara_texto(nome, "no-messages") == 0) { o.sem_mensagens = 1; return 0; }
    if (compara_texto(nome, "files-with-matches") == 0) { o.listar_modo = LISTAR_COM; return 0; }
    if (compara_texto(nome, "files-without-match") == 0) { o.listar_modo = LISTAR_SEM; return 0; }
    if (compara_texto(nome, "with-filename") == 0) { o.forca_nome = 1; return 0; }
    if (compara_texto(nome, "no-filename") == 0) { o.forca_nome = 2; return 0; }
    if (compara_texto(nome, "null") == 0) { o.nulo = 1; return 0; }
    if (compara_texto(nome, "recursive") == 0 ||
        compara_texto(nome, "dereference-recursive") == 0) {
        o.recursivo = 1; o.dir_modo = DIR_RECURSIVO; return 0;
    }
    if (compara_texto(nome, "text") == 0) { o.binario = BINARIO_TEXTO; return 0; }
    if (compara_texto(nome, "binary") == 0) { o.binario = BINARIO_PADRAO; return 0; }
    if (compara_texto(nome, "line-buffered") == 0) return 0;
    if (compara_texto(nome, "mmap") == 0 || compara_texto(nome, "no-mmap") == 0) return 0;
    if (compara_texto(nome, "initial-tab") == 0) { o.tab = 1; return 0; }
    if (compara_texto(nome, "unix-byte-offsets") == 0) return 0;
    if (compara_texto(nome, "no-group-separator") == 0) { o.grupo_sep = 0; return 0; }
    if (compara_texto(nome, "null-data") == 0) {
        imprimir_em(ERROS, "grep: -z/--null-data não é suportado\n");
        return -1;
    }
    if (compara_texto(nome, "color") == 0 || compara_texto(nome, "colour") == 0) {
        if (!val) {
            o.cor = 0;   /* --color sozinho é "auto"; aqui, auto = never */
        } else if (compara_texto(val, "always") == 0 ||
                   compara_texto(val, "yes") == 0 || compara_texto(val, "force") == 0) {
            o.cor = 1;
        } else if (compara_texto(val, "never") == 0 ||
                   compara_texto(val, "no") == 0 || compara_texto(val, "none") == 0) {
            o.cor = 0;
        } else if (compara_texto(val, "auto") == 0 ||
                   compara_texto(val, "tty") == 0 || compara_texto(val, "if-tty") == 0) {
            o.cor = 0;   /* sem detecção de terminal: auto = never */
        } else {
            imprimir_em(ERROS, "grep: --color=%s: valor inválido\n", val);
            return -1;
        }
        return 0;
    }

    /* Opções com argumento. */
    if (!val && (compara_texto(nome, "regexp") == 0 ||
                 compara_texto(nome, "file") == 0 ||
                 compara_texto(nome, "include") == 0 ||
                 compara_texto(nome, "exclude") == 0 ||
                 compara_texto(nome, "exclude-from") == 0 ||
                 compara_texto(nome, "exclude-dir") == 0 ||
                 compara_texto(nome, "binary-files") == 0 ||
                 compara_texto(nome, "max-count") == 0 ||
                 compara_texto(nome, "after-context") == 0 ||
                 compara_texto(nome, "before-context") == 0 ||
                 compara_texto(nome, "group-separator") == 0 ||
                 compara_texto(nome, "label") == 0 ||
                 compara_texto(nome, "directories") == 0 ||
                 compara_texto(nome, "devices") == 0)) {
        if (*pi + 1 >= argc) {
            imprimir_em(ERROS, "grep: --%s precisa de um argumento\n", nome);
            return -1;
        }
        (*pi)++;
        val = argv[*pi];
    }

    if (compara_texto(nome, "regexp") == 0) {
        if (adicionar_padrao(val) < 0) return -1;
        return 0;
    }
    if (compara_texto(nome, "file") == 0)
        return ler_padroes_de(val) < 0 ? -1 : 0;
    if (compara_texto(nome, "include") == 0) {
        if (n_include >= MAX_INCLUDE) {
            imprimir_em(ERROS, "grep: --include demais\n");
            return -1;
        }
        include[n_include++] = val;
        return 0;
    }
    if (compara_texto(nome, "exclude") == 0) {
        if (n_exclude >= MAX_EXCLUDE) {
            imprimir_em(ERROS, "grep: --exclude demais\n");
            return -1;
        }
        exclude[n_exclude++] = val;
        return 0;
    }
    if (compara_texto(nome, "exclude-from") == 0)
        return ler_excludes_de(val) < 0 ? -1 : 0;
    if (compara_texto(nome, "exclude-dir") == 0) {
        if (n_exclude_dir >= MAX_EXCLUDE_DIR) {
            imprimir_em(ERROS, "grep: --exclude-dir demais\n");
            return -1;
        }
        exclude_dir[n_exclude_dir++] = val;
        return 0;
    }
    if (compara_texto(nome, "binary-files") == 0) {
        if (compara_texto(val, "binary") == 0) o.binario = BINARIO_PADRAO;
        else if (compara_texto(val, "text") == 0) o.binario = BINARIO_TEXTO;
        else if (compara_texto(val, "without-match") == 0) o.binario = BINARIO_SEM;
        else {
            imprimir_em(ERROS, "grep: --binary-files=%s: valor inválido\n", val);
            return -1;
        }
        return 0;
    }
    if (compara_texto(nome, "max-count") == 0) {
        int v;
        if (!numero(val, &v)) {
            imprimir_em(ERROS, "grep: --max-count=%s: número inválido\n", val);
            return -1;
        }
        o.max_cont = v;
        return 0;
    }
    if (compara_texto(nome, "after-context") == 0 ||
        compara_texto(nome, "before-context") == 0 ||
        compara_texto(nome, "context") == 0) {
        int v = 2;
        if (val && !numero(val, &v)) {
            imprimir_em(ERROS, "grep: --%s=%s: número inválido\n", nome, val);
            return -1;
        }
        o.contexto_pedido = 1;
        if (compara_texto(nome, "after-context") != 0) o.antes = v;
        if (compara_texto(nome, "before-context") != 0) o.depois = v;
        return 0;
    }
    if (compara_texto(nome, "group-separator") == 0) {
        o.sep_grupo = val;
        o.grupo_sep = 1;
        return 0;
    }
    if (compara_texto(nome, "label") == 0) { o.rotulo_stdin = val; return 0; }
    if (compara_texto(nome, "directories") == 0) {
        if (compara_texto(val, "read") == 0) o.dir_modo = DIR_LER;
        else if (compara_texto(val, "skip") == 0) o.dir_modo = DIR_PULAR;
        else if (compara_texto(val, "recurse") == 0) {
            o.dir_modo = DIR_RECURSIVO;
            o.recursivo = 1;
        } else {
            imprimir_em(ERROS, "grep: --directories=%s: valor inválido\n", val);
            return -1;
        }
        return 0;
    }
    if (compara_texto(nome, "devices") == 0) return 0;

    imprimir_em(ERROS, "grep: opção não reconhecida '%s'\n", a);
    return -1;
}

static int opcao_curta(char **argv, int argc, int *pi)
{
    char *a = argv[*pi];
    for (char *p = a + 1; *p; p++) {
        switch (*p) {
        case 'E': o.modo = MODO_ERE; break;
        case 'F': o.modo = MODO_FIXO; break;
        case 'G': o.modo = MODO_BRE; break;
        case 'i': case 'y': o.caso_insensivel = 1; break;
        case 'v': o.inverter = 1; break;
        case 'w': o.palavra = 1; break;
        case 'x': o.linha_inteira = 1; break;
        case 'n': o.num_linha = 1; break;
        case 'b': o.desloc_byte = 1; break;
        case 'c': o.contar = 1; break;
        case 'o': o.somente = 1; break;
        case 'q': o.silencioso = 1; break;
        case 's': o.sem_mensagens = 1; break;
        case 'l': o.listar_modo = LISTAR_COM; break;
        case 'L': o.listar_modo = LISTAR_SEM; break;
        case 'H': o.forca_nome = 1; break;
        case 'h': o.forca_nome = 2; break;
        case 'Z': o.nulo = 1; break;
        case 'a': o.binario = BINARIO_TEXTO; break;
        case 'I': o.binario = BINARIO_SEM; break;
        case 'U': case 'u': break;
        case 'T': o.tab = 1; break;
        case 'V':
            s_flush();
            imprimir("grep (SaguiOS) 1.0 - H50\n");
            return 1;
        case 'r': case 'R':
            o.recursivo = 1;
            o.dir_modo = DIR_RECURSIVO;
            break;
        case 'P':
            imprimir_em(ERROS, "grep: -P (PCRE) não é suportado\n");
            return -1;
        case 'z':
            imprimir_em(ERROS, "grep: -z não é suportado\n");
            return -1;
        case 'e': {
            const char *v = arg_curto(argv, argc, pi, &p);
            if (!v) { imprimir_em(ERROS, "grep: -e precisa de um padrão\n"); return -1; }
            if (adicionar_padrao(v) < 0) return -1;
            break;
        }
        case 'f': {
            const char *v = arg_curto(argv, argc, pi, &p);
            if (!v) { imprimir_em(ERROS, "grep: -f precisa de um arquivo\n"); return -1; }
            if (ler_padroes_de(v) < 0) return -1;
            break;
        }
        case 'm': case 'A': case 'B': case 'C': {
            char opt = *p;
            const char *v = arg_curto(argv, argc, pi, &p);
            int n;
            if (!v) {
                imprimir_em(ERROS, "grep: -%c precisa de um número\n", opt);
                return -1;
            }
            if (!numero(v, &n)) {
                imprimir_em(ERROS, "grep: -%c%s: número inválido\n", opt, v);
                return -1;
            }
            if (opt == 'm') o.max_cont = n;
            else {
                o.contexto_pedido = 1;
                if (opt == 'A') o.depois = n;
                else if (opt == 'B') o.antes = n;
                else { o.antes = n; o.depois = n; }
            }
            break;
        }
        case 'd': {
            const char *v = arg_curto(argv, argc, pi, &p);
            if (!v) { imprimir_em(ERROS, "grep: -d precisa de uma ação\n"); return -1; }
            if (compara_texto(v, "read") == 0) o.dir_modo = DIR_LER;
            else if (compara_texto(v, "skip") == 0) o.dir_modo = DIR_PULAR;
            else if (compara_texto(v, "recurse") == 0) {
                o.dir_modo = DIR_RECURSIVO;
                o.recursivo = 1;
            } else {
                imprimir_em(ERROS, "grep: -d%s: ação inválida\n", v);
                return -1;
            }
            break;
        }
        case 'D': {
            const char *v = arg_curto(argv, argc, pi, &p);
            if (!v) { imprimir_em(ERROS, "grep: -D precisa de uma ação\n"); return -1; }
            break;
        }
        default:
            if (*p >= '0' && *p <= '9') {
                int n = 0;
                while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
                p--;
                o.contexto_pedido = 1;
                o.antes = n;
                o.depois = n;
            } else {
                imprimir_em(ERROS, "grep: opção inválida -- '%c'\n", *p);
                return -1;
            }
            break;
        }
    }
    return 0;
}

/* Há -e/-f/--regexp/--file?  Se sim, o primeiro nome solto é arquivo (como
 * o getopt permutado do GNU). */
static int tem_opcao_padrao(int argc, char **argv)
{
    int fim = 0;
    for (int i = 1; i < argc; i++) {
        char *a = argv[i];
        if (fim || a[0] != '-' || !a[1]) continue;
        if (a[1] == '-') {
            char *nome = a + 2;
            unsigned n = 0;
            if (!*nome) { fim = 1; continue; }
            while (nome[n] && nome[n] != '=') n++;
            if ((n == 6 && compara_texto_n(nome, "regexp", 6) == 0) ||
                (n == 4 && compara_texto_n(nome, "file", 4) == 0))
                return 1;
        } else {
            for (char *p = a + 1; *p; p++) {
                if (*p == 'e' || *p == 'f') return 1;
                if (*p == 'A' || *p == 'B' || *p == 'C' ||
                    *p == 'd' || *p == 'D' || *p == 'm') break;
            }
        }
    }
    return 0;
}

/* --------------------------------------------------------------- principal */

int principal(int argc, char **argv)
{
    int i;
    int tem_padrao = 0;
    int fim_opcoes = 0;

    o.modo = MODO_BRE;
    o.listar_modo = LISTAR_NENHUM;
    o.dir_modo = DIR_LER;
    o.binario = BINARIO_PADRAO;
    o.max_cont = -1;
    o.grupo_sep = 1;
    o.sep_grupo = "--";

    tem_padrao = tem_opcao_padrao(argc, argv);

    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (!fim_opcoes && a[0] == '-' && a[1] != 0) {
            if (a[1] == '-') {
                if (a[2] == 0) { fim_opcoes = 1; continue; }
                int r = opcao_longa(argv, argc, &i);
                if (r > 0) { s_flush(); return 0; }
                if (r < 0) { s_flush(); return 2; }
            } else {
                int r = opcao_curta(argv, argc, &i);
                if (r > 0) { s_flush(); return 0; }
                if (r < 0) { s_flush(); return 2; }
            }
            continue;
        }
        if (!tem_padrao && npad == 0) {
            if (adicionar_padrao(a) < 0) { s_flush(); return 2; }
            tem_padrao = 1;
        } else {
            if (n_operandos >= MAX_OPERANDOS) {
                imprimir_em(ERROS, "grep: arquivos demais (máximo %d)\n", MAX_OPERANDOS);
                s_flush();
                return 2;
            }
            operandos[n_operandos++] = a;
        }
    }

    if (!tem_padrao && npad == 0) {
        imprimir_em(ERROS, "grep: falta o padrão\n");
        imprimir_em(ERROS, "uso: grep [opções] padrão [arquivo...]\n");
        s_flush();
        return 2;
    }

    /* -q, -l/-L e -c/-o têm precedência, como no GNU grep. */
    if (o.silencioso) {
        o.contar = 0;
        o.somente = 0;
        o.listar_modo = LISTAR_NENHUM;
    } else if (o.listar_modo != LISTAR_NENHUM) {
        o.contar = 0;
        o.somente = 0;
    } else if (o.contar) {
        o.somente = 0;
    }

    /* Compila as expressões. */
    if (o.modo != MODO_FIXO) {
        int opcoes_exp = 0;
        if (o.modo == MODO_ERE) opcoes_exp |= EXP_ESTENDIDA;
        if (o.caso_insensivel) opcoes_exp |= EXP_CASO_INSENSIVEL;
        if (o.palavra) opcoes_exp |= EXP_PALAVRA_INTEIRA;
        if (o.linha_inteira) opcoes_exp |= EXP_LINHA_INTEIRA;
        for (int k = 0; k < npad; k++) {
            res[k] = (expressao_t *)aloca(sizeof(expressao_t));
            if (!res[k]) {
                imprimir_em(ERROS, "grep: sem memória\n");
                s_flush();
                return 2;
            }
            if (!expressao_compilar(res[k], padroes[k], opcoes_exp)) {
                imprimir_em(ERROS, "grep: %s\n", expressao_erro(res[k]));
                s_flush();
                return 2;
            }
        }
    }

    /* -m0 sem -L não abre nada (nem reclama de arquivos que não existem). */
    if (o.max_cont == 0 && o.listar_modo != LISTAR_SEM) {
        s_flush();
        return 1;
    }

    if (o.recursivo && n_operandos == 0) {
        /* Sem arquivos, o -r procura na pasta atual.  O nome sai sem "./". */
        percorrer(0, ".", "");
        s_flush();
        if (o.silencioso && achou_algum) return 0;
        if (houve_erro) return 2;
        return achou_algum ? 0 : 1;
    }

    if (n_operandos == 0) {
        const char *nome = o.rotulo_stdin ? o.rotulo_stdin : "(standard input)";
        int mostrar = (o.forca_nome == 1);
        if (o.max_cont == 0) {
            if (o.listar_modo == LISTAR_SEM && !o.silencioso)
                imprimir_nome_lista(nome);
        } else {
            processar_arquivo("-", nome, mostrar);
        }
    } else {
        for (int k = 0; k < n_operandos && !parar_tudo; k++) {
            const char *real = operandos[k];
            const char *exib;
            if (compara_texto(real, "-") == 0)
                exib = o.rotulo_stdin ? o.rotulo_stdin : "(standard input)";
            else
                exib = real;
            if (o.max_cont == 0)
                listar_sem_ler(real, exib);
            else
                processar_operando(real, exib);
        }
    }

    s_flush();
    if (o.silencioso && achou_algum) return 0;
    if (houve_erro) return 2;
    return achou_algum ? 0 : 1;
}
