/* ide [arquivo] - o editor de codigo do SaguiOS.
 *
 * E o programa padrao para abrir .go, .lua, .c, .h e .mk (receitas do make).
 * Tem realce de sintaxe, numeros de linha, barra de status, auto-indentacao,
 * localizar (Ctrl+F), ir para linha (Ctrl+G), modelos prontos para cada
 * linguagem e um painel com a saida da compilacao/execucao.
 *
 * Go e compilado pelo gosagui; Lua e interpretado; C e compilado pelo tinycc;
 * .mk e compilado pelo make. "Executar" salva antes de compilar. O texto e
 * Latin-1, como em todo o sistema. */
#include "sagui.h"
#include "gui.h"

#define LARG 760
#define ALT  480
#define BARRA 72
#define STATUS_Y 346
#define STATUS_ALT 18
#define SAIDA_Y 366
#define SAIDA_ALT 108

enum { L_NENHUMA, L_GO, L_LUA, L_C, L_MAKEFILE };

typedef struct {
    area_t    *area;
    int        lingua;
    const char *linha_cache;   /* cache do estado de comentario de bloco */
    int        bloco_cache;
} realce_t;

static jan_t *j;
static area_t area;
static char caminho[160];
static bool modificado_mostrado = true;
static int filho = -1, pipe_saida = -1, etapa = 0, codigo_filho;
static bool terminou = false, gerou_temp = false, rodar_depois = true;
static char executavel[200];
static char saida[8192];
static int saida_tam;
static int lingua = L_NENHUMA;
static realce_t realce;
static char ultima_busca[96];
static char aviso[96];

static void definir_lingua(int lg)
{
    lingua = lg;
    realce.lingua = lg;
    realce.linha_cache = NULL;
}

/* ------------------------------------------------------------------ lingua */

static const char *nome_lingua(void)
{
    switch (lingua) {
    case L_GO:       return "Go";
    case L_LUA:      return "Lua";
    case L_C:        return "C (tinycc)";
    case L_MAKEFILE: return "Makefile";
    default:         return "Texto";
    }
}

static int lingua_do_caminho(const char *c)
{
    const char *p = acha_ultimo_char(c, '.');
    if (!p) return L_NENHUMA;
    if (compara_texto(p, ".go") == 0) return L_GO;
    if (compara_texto(p, ".lua") == 0) return L_LUA;
    if (compara_texto(p, ".c") == 0 || compara_texto(p, ".h") == 0) return L_C;
    if (compara_texto(p, ".mk") == 0) return L_MAKEFILE;
    return L_NENHUMA;
}

/* --------------------------------------------------------------- posicao */

static void posicao_atual(int *lin, int *col)
{
    int l = 0, ini = 0;
    for (int i = 0; i < area.cursor && i < area.tam; i++)
        if (area.buf[i] == '\n') { l++; ini = i + 1; }
    *lin = l;
    *col = area.cursor - ini;
}

static int inicio_da_linha_ide(int lin)
{
    int pos = 0;
    for (int l = 0; l < lin && pos < area.tam; pos++)
        if (area.buf[pos] == '\n') l++;
    return pos;
}

static int total_linhas_ide(void)
{
    int n = 1;
    for (int i = 0; i < area.tam; i++) if (area.buf[i] == '\n') n++;
    return n;
}

/* -------------------------------------------------------------- realce */

static bool id_inicio(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static bool id_caractere(int c)
{
    return id_inicio(c) || (c >= '0' && c <= '9');
}

static bool palavra_em(const char *p, int n, const char *const *lista)
{
    for (; *lista; lista++) {
        const char *q = *lista;
        int i = 0;
        while (q[i] && i < n && q[i] == p[i]) i++;
        if (!q[i] && i == n) return true;
    }
    return false;
}

static const char *const palavras_go[] = {
    "break", "case", "chan", "const", "continue", "default", "defer", "else",
    "fallthrough", "for", "func", "go", "goto", "if", "import", "interface",
    "map", "package", "range", "return", "select", "struct", "switch", "type",
    "var", "bool", "byte", "complex64", "complex128", "error", "float32",
    "float64", "int", "int8", "int16", "int32", "int64", "rune", "string",
    "uint", "uint8", "uint16", "uint32", "uint64", "uintptr", "true", "false",
    "nil", NULL
};

static const char *const palavras_lua[] = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
    "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return",
    "then", "true", "until", "while", "require", "print", "pairs", "ipairs",
    "string", "math", "table", "io", "os", "type", "tostring", "tonumber",
    NULL
};

static const char *const palavras_c[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if", "inline",
    "int", "long", "register", "restrict", "return", "short", "signed",
    "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned",
    "void", "volatile", "while", "bool", "true", "false", "NULL", "size_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t", "int16_t",
    "int32_t", "int64_t", "va_list", "include", "define", "ifdef", "ifndef",
    "endif", "pragma", NULL
};

static const char *const palavras_mk[] = {
    "NOME", "FONTE", "SRC", "FONTES", "SOURCES", "SAIDA", "ALVO", "TARGET",
    "OUT", "BIBLIOTECAS", "LIBS", "OPCOES", "CFLAGS", "FLAGS", "INCLUI",
    "INCLUDES", "COMPILADOR", "CC", "EXECUTAR", "RUN", "sim", "nao", NULL
};

/* Estado de comentario de bloco no inicio da linha (so C). O cache evita
 * varrer o arquivo inteiro para cada linha visivel. */
static int bloco_no_inicio(realce_t *r, const char *linha)
{
    if (r->linha_cache == linha) return r->bloco_cache;
    int dentro = 0;
    const char *p = r->area->buf;
    while (p < linha) {
        if (!dentro && p + 1 < linha && p[0] == '/' && p[1] == '*') {
            dentro = 1; p += 2; continue;
        }
        if (dentro && p + 1 < linha && p[0] == '*' && p[1] == '/') {
            dentro = 0; p += 2; continue;
        }
        p++;
    }
    r->linha_cache = linha;
    r->bloco_cache = dentro;
    return dentro;
}

#define COR_PALAVRA    0x0B5CAD
#define COR_STRING     0x0B7A3E
#define COR_COMENTARIO 0x6A737C
#define COR_NUMERO     0xB35C00
#define COR_PREPROC    0x8A4B00

static uint32_t cor_sintaxe(const char *linha, int n, int pos, int *consumir, void *ctx)
{
    realce_t *r = ctx;
    int c = (unsigned char)linha[pos];
    *consumir = 1;

    /* Comentario de bloco em C, inclusive quando comeca numa linha anterior. */
    if (r->lingua == L_C) {
        int dentro = bloco_no_inicio(r, linha);
        if (dentro) {
            for (int i = pos; i + 1 < n; i++)
                if (linha[i] == '*' && linha[i + 1] == '/') {
                    *consumir = i + 2 - pos;
                    return COR_COMENTARIO;
                }
            *consumir = n - pos;
            return COR_COMENTARIO;
        }
        if (c == '/' && pos + 1 < n && linha[pos + 1] == '*') {
            for (int i = pos + 2; i + 1 < n; i++)
                if (linha[i] == '*' && linha[i + 1] == '/') {
                    *consumir = i + 2 - pos;
                    return COR_COMENTARIO;
                }
            *consumir = n - pos;
            return COR_COMENTARIO;
        }
        if (c == '#') {
            bool so_espacos = true;
            for (int i = 0; i < pos; i++) if (!eh_espaco(linha[i])) so_espacos = false;
            if (so_espacos) { *consumir = n - pos; return COR_PREPROC; }
        }
    }

    /* Comentario de uma linha. */
    if ((r->lingua == L_C || r->lingua == L_GO) &&
        c == '/' && pos + 1 < n && linha[pos + 1] == '/') {
        *consumir = n - pos; return COR_COMENTARIO;
    }
    if (r->lingua == L_LUA && c == '-' && pos + 1 < n && linha[pos + 1] == '-') {
        *consumir = n - pos; return COR_COMENTARIO;
    }
    if (r->lingua == L_MAKEFILE && c == '#') {
        *consumir = n - pos; return COR_COMENTARIO;
    }

    /* Strings e caracteres. */
    if (c == '"' || c == '\'') {
        int i = pos + 1;
        bool escape = false;
        while (i < n) {
            if (escape) escape = false;
            else if (linha[i] == '\\') escape = true;
            else if (linha[i] == c) { i++; break; }
            i++;
        }
        *consumir = i - pos;
        return COR_STRING;
    }

    /* Numeros. */
    if (c >= '0' && c <= '9') {
        int i = pos + 1;
        while (i < n && (id_caractere((unsigned char)linha[i]) || linha[i] == '.'))
            i++;
        *consumir = i - pos;
        return COR_NUMERO;
    }

    /* Identificadores e palavras-chave. */
    if (id_inicio(c)) {
        int i = pos + 1;
        while (i < n && id_caractere((unsigned char)linha[i])) i++;
        *consumir = i - pos;
        const char *const *lista = r->lingua == L_GO ? palavras_go :
                                   r->lingua == L_LUA ? palavras_lua :
                                   r->lingua == L_C ? palavras_c :
                                   r->lingua == L_MAKEFILE ? palavras_mk : NULL;
        if (lista && palavra_em(linha + pos, i - pos, lista)) return COR_PALAVRA;
        return COR_TEXTO;
    }

    return COR_TEXTO;
}

/* --------------------------------------------------------------- status */

static void desenhar_status(void)
{
    int lin, col;
    char t[180];
    posicao_atual(&lin, &col);
    formatar_texto(t, sizeof t, "%s  |  Ln %d, Col %d  |  %d bytes%s",
                   nome_lingua(), lin + 1, col + 1, area.tam,
                   area.modificado ? "  *" : "");
    g_retangulo(j, 6, STATUS_Y, LARG - 12, STATUS_ALT, 0xE6E9EC);
    g_retangulo(j, 6, STATUS_Y, LARG - 12, 1, COR_BORDA);
    g_texto_n(j, 10, STATUS_Y + 2, t, (LARG - 30) / LETRA_L, COR_TEXTO_FRACO);
    if (aviso[0]) {
        int x = LARG - 10 - g_largura_texto(aviso);
        if (x < 340) x = 340;
        g_texto(j, x, STATUS_Y + 2, aviso, COR_DESTAQUE);
    }
    jan_mostrar_area(j, 6, STATUS_Y, LARG - 12, STATUS_ALT);
}

static void redesenhar(void)
{
    realce.linha_cache = NULL;
    area_desenhar(j, &area);
    desenhar_status();
}

/* ------------------------------------------------------------------ saida */

static void mostrar_saida(void)
{
    g_retangulo(j, 6, SAIDA_Y, LARG - 12, SAIDA_ALT, 0x12161E);
    int inicio = 0, linhas = 1;
    for (int i = saida_tam - 1; i >= 0; i--) {
        if (saida[i] == '\n' && ++linhas > 6) { inicio = i + 1; break; }
    }
    int x = 10, y = SAIDA_Y + 4;
    for (int i = inicio; i < saida_tam && y < SAIDA_Y + SAIDA_ALT - 14; i++) {
        char c = saida[i];
        if (c == '\n') { x = 10; y += 16; }
        else if (c >= 32 && x < LARG - 16) { g_texto_n(j, x, y, &c, 1, 0xD8DDE2); x += 8; }
    }
    jan_mostrar_area(j, 6, SAIDA_Y, LARG - 12, SAIDA_ALT);
}

static void acrescentar(const char *s, int n)
{
    if (n > (int)sizeof saida - 1) { s += n - (sizeof saida - 1); n = sizeof saida - 1; }
    if (saida_tam + n >= (int)sizeof saida) {
        int remover = saida_tam + n - ((int)sizeof saida - 1);
        memmove(saida, saida + remover, (size_t)(saida_tam - remover));
        saida_tam -= remover;
    }
    memcpy(saida + saida_tam, s, (size_t)n); saida_tam += n; saida[saida_tam] = 0;
    escrever(SAIDA, s, (unsigned)n); /* espelho para o log do terminal */
    mostrar_saida();
}

/* Redireciona somente os descritores herdados pelo filho; nao usa shell. */
static bool iniciar(const char *programa, char **args)
{
    int p[2];
    if (criar_pipe(p) < 0) return false;
    int a = duplicar(SAIDA, 14), b = duplicar(ERROS, 15);
    if (a < 0 || b < 0) {
        if (a >= 0) fechar(a);
        if (b >= 0) fechar(b);
        fechar(p[0]); fechar(p[1]); return false;
    }
    duplicar(p[1], SAIDA); duplicar(p[1], ERROS);
    filho = executar(programa, args);
    duplicar(a, SAIDA); duplicar(b, ERROS);
    fechar(a); fechar(b); fechar(p[1]);
    if (filho < 0) { fechar(p[0]); filho = -1; return false; }
    pipe_saida = p[0]; terminou = false;
    return true;
}

static void acompanhar(void)
{
    if (pipe_saida < 0) return;
    int d = disponivel(pipe_saida);
    if (d > 0) {
        char b[512]; int n = ler(pipe_saida, b, sizeof b);
        if (n > 0) acrescentar(b, n);
    }
    if (!terminou && filho > 0 && recolher(&codigo_filho) == filho) terminou = true;
    if (terminou && disponivel(pipe_saida) <= 0) {
        fechar(pipe_saida); pipe_saida = -1; filho = -1;
        if (etapa == 1 && codigo_filho == 0 && rodar_depois) {
            etapa = 2;
            acrescentar("\nCompilado. Executando...\n", (int)tam_texto("\nCompilado. Executando...\n"));
            char *args[] = { executavel, NULL };
            if (iniciar(executavel, args)) return;
            acrescentar("Erro ao iniciar executavel.\n", (int)tam_texto("Erro ao iniciar executavel.\n"));
        } else {
            char m[80];
            formatar_texto(m, sizeof m, "\nTerminado (codigo %d).\n", codigo_filho);
            acrescentar(m, (int)tam_texto(m));
        }
        if (gerou_temp) apagar(executavel);
        gerou_temp = false; etapa = 0; rodar_depois = true;
    }
}

/* ------------------------------------------------------------- arquivo */

static const char *nome_curto(void)
{
    if (!caminho[0]) return "sem nome";
    const char *b = acha_ultimo_char(caminho, '/');
    return b ? b + 1 : caminho;
}

static void atualizar_titulo(void)
{
    if (modificado_mostrado == area.modificado) return;
    modificado_mostrado = area.modificado;
    char t[96];
    formatar_texto(t, sizeof t, "IDE - %s%s", nome_curto(), area.modificado ? " *" : "");
    titulo_janela(j->id, t);
    registrar("ide: titulo %s\n", t);
}

static void forcar_titulo(void)
{
    modificado_mostrado = !area.modificado;
    atualizar_titulo();
}

static bool abrir_arquivo(const char *c)
{
    int fd = abrir(c, ABRIR_LER);
    if (fd < 0) {
        char m[200];
        formatar_texto(m, sizeof m, "Não consegui abrir\n%s\n(%s)", c, nome_erro(fd));
        mensagem("IDE", m);
        return false;
    }
    char *buf = aloca(65536);
    int n = 0, r;
    while (n < 65535 && (r = ler(fd, buf + n, (unsigned)(65535 - n))) > 0) n += r;
    fechar(fd);
    n = (int)utf8_para_latin1(buf, (size_t)n, NULL);   /* textos de fora costumam vir em UTF-8 (D027) */
    area_definir(&area, buf, n);
    libera(buf);
    copia_texto(caminho, c, sizeof caminho);
    definir_lingua(lingua_do_caminho(caminho));
    area.marca_tam = 0;
    ultima_busca[0] = 0; aviso[0] = 0;
    registrar("ide: aberto %s (%d bytes)\n", caminho, n);
    forcar_titulo();
    redesenhar();
    return true;
}

static bool salvar_como(void);

static bool salvar(void)
{
    if (!caminho[0]) return salvar_como();
    int fd = abrir(caminho, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd < 0 || escrever_tudo(fd, area.buf, (unsigned)area.tam) != area.tam) {
        if (fd >= 0) fechar(fd);
        char m[200];
        formatar_texto(m, sizeof m, "Não consegui salvar\n%s", caminho);
        mensagem("IDE", m);
        return false;
    }
    fechar(fd);
    area.modificado = false;
    definir_lingua(lingua_do_caminho(caminho));
    registrar("ide: salvo %s (%d bytes)\n", caminho, area.tam);
    forcar_titulo();
    desenhar_status();
    return true;
}

static bool salvar_como(void)
{
    char c[160];
    copia_texto(c, caminho[0] ? caminho : "/tmp/", sizeof c);
    if (!pedir_texto("Salvar como", "Salvar o arquivo em (ex.: /tmp/meu.c):", c, sizeof c) || !c[0])
        return false;
    copia_texto(caminho, c, sizeof caminho);
    return salvar();
}

/* Antes de jogar o texto fora: pergunta se quer salvar. Devolve false se a
 * pessoa quis salvar e não deu certo (aí não descarta nada). */
static bool pode_descartar(void)
{
    if (!area.modificado) return true;
    if (confirmar("IDE", "O texto tem alterações não salvas.\nSalvar antes?"))
        return salvar();
    return true;
}

/* --------------------------------------------------------------- modelos */

typedef struct {
    const char *nome;
    int         lingua;
    const char *texto;
} modelo_t;

static const modelo_t modelos[] = {
    { "Go", L_GO,
      "package main\n\nimport \"fmt\"\n\nfunc main() {\n    fmt.Println(\"Ola, SaguiOS!\")\n}\n" },
    { "Lua", L_LUA,
      "-- Exemplo Lua\nfor i = 1, 3 do\n    print(\"Ola, SaguiOS!\", i)\nend\n" },
    { "C (tinycc)", L_C,
      "/* Ola em C, compilado pelo tinycc dentro do SaguiOS. */\n"
      "#include <stdio.h>\n\n"
      "int main(void)\n{\n"
      "    printf(\"Ola, SaguiOS!\\n\");\n"
      "    return 0;\n"
      "}\n" },
    { "Makefile (.mk)", L_MAKEFILE,
      "# Receita do make. Compile com: make este-arquivo.mk\n"
      "NOME        = Ola em C\n"
      "FONTE       = /tcc/hello-tcc.c\n"
      "SAIDA       = ola-tcc\n"
      "BIBLIOTECAS =\n"
      "OPCOES      = -static\n"
      "COMPILADOR  = /bin/tinycc\n"
      "EXECUTAR    = nao\n" },
    { NULL, L_NENHUMA, NULL }
};

static int escolher_modelo(void)
{
    jan_t *d = jan_abrir("Modelo de codigo", 360, 200);
    if (!d) return -1;
    botao_t bs[5] = {
        { 20, 14, 150, 34, "Go", false }, { 190, 14, 150, 34, "Lua", false },
        { 20, 58, 150, 34, "C (tinycc)", false }, { 190, 58, 150, 34, "Makefile (.mk)", false },
        { 20, 110, 320, 34, "Cancelar", false },
    };
    rotulo(d, 20, 158, "O texto entra como arquivo novo (salve com a extensão).", COR_TEXTO_FRACO);
    for (int i = 0; i < 5; i++) botao_desenhar(d, &bs[i]);
    jan_mostrar(d);

    int resposta = -1;
    evento_t e;
    for (;;) {
        if (proximo_evento(d->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) break;
        if (e.tipo == EV_TECLA && e.tecla == TECLA_ESC) break;
        for (int i = 0; i < 5; i++)
            if (botao_tratar(d, &bs[i], &e)) { resposta = i < 4 ? i : -1; break; }
        if (resposta >= 0) break;
        if (botao_tratar(d, &bs[4], &e)) break;
    }
    jan_fechar(d);
    if (resposta >= 0) registrar("ide: modelo %s\n", modelos[resposta].nome);
    return resposta;
}

static void exemplo_modelo(int i)
{
    if (i < 0 || !modelos[i].nome || !pode_descartar()) return;
    area_definir(&area, modelos[i].texto, (int)tam_texto(modelos[i].texto));
    area.modificado = true;
    caminho[0] = 0;
    definir_lingua(modelos[i].lingua);
    area.marca_tam = 0;
    ultima_busca[0] = 0; aviso[0] = 0;
    forcar_titulo();
    redesenhar();
}

/* -------------------------------------------------------------- .mk */

static void aparar_mk(char *s)
{
    char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    if (p != s) memmove(s, p, tam_texto(p) + 1);
    size_t n = tam_texto(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
        s[--n] = 0;
}

static void tirar_aspas_mk(char *s)
{
    size_t n = tam_texto(s);
    if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') ||
                   (s[0] == '\'' && s[n - 1] == '\''))) {
        memmove(s, s + 1, n - 1);
        s[n - 2] = 0;
    }
}

static void primeiro_token(const char *s, char *dst, size_t max)
{
    while (*s == ' ' || *s == '\t') s++;
    size_t n = 0;
    while (s[n] && s[n] != ' ' && s[n] != '\t' && n + 1 < max) { dst[n] = s[n]; n++; }
    dst[n] = 0;
}

static void nome_sem_extensao(const char *c, char *dst, size_t max)
{
    const char *b = acha_ultimo_char(c, '/');
    b = b ? b + 1 : c;
    copia_texto(dst, b, max);
    char *p = acha_ultimo_char(dst, '.');
    if (p && p != dst) *p = 0;
}

/* Le a receita .mk para saber o executavel gerado e se o make ja deve
 * executa-lo (EXECUTAR = sim). Devolve false se nao conseguiu ler. */
static bool ler_receita_mk(const char *mk, char *saida_mk, size_t max, bool *executa)
{
    char buf[4096];
    *executa = false;
    saida_mk[0] = 0;
    int fd = abrir(mk, ABRIR_LER);
    if (fd < 0) return false;
    int n = ler(fd, buf, sizeof buf - 1);
    fechar(fd);
    if (n < 0) return false;
    buf[n] = 0;

    char fonte[256] = "";
    char *p = buf;
    while (*p) {
        char *fim = acha_char(p, '\n');
        if (fim) *fim = 0;
        char *linha = p;
        aparar_mk(linha);
        if (*linha && *linha != '#') {
            char *igual = acha_char(linha, '=');
            if (igual) {
                *igual = 0;
                char *chave = linha, *valor = igual + 1;
                aparar_mk(chave); aparar_mk(valor); tirar_aspas_mk(valor);
                if (compara_texto(chave, "SAIDA") == 0 || compara_texto(chave, "ALVO") == 0 ||
                    compara_texto(chave, "TARGET") == 0 || compara_texto(chave, "OUT") == 0)
                    copia_texto(saida_mk, valor, max);
                else if (compara_texto(chave, "FONTE") == 0 || compara_texto(chave, "SRC") == 0 ||
                         compara_texto(chave, "FONTES") == 0 || compara_texto(chave, "SOURCES") == 0) {
                    if (!fonte[0]) primeiro_token(valor, fonte, sizeof fonte);
                } else if (compara_texto(chave, "EXECUTAR") == 0 || compara_texto(chave, "RUN") == 0) {
                    *executa = compara_texto(valor, "sim") == 0 || compara_texto(valor, "1") == 0 ||
                               compara_texto(valor, "yes") == 0 || compara_texto(valor, "true") == 0;
                }
            }
        }
        if (!fim) break;
        p = fim + 1;
    }
    if (!saida_mk[0] && fonte[0]) nome_sem_extensao(fonte, saida_mk, max);
    return true;
}

static void caminho_absoluto(const char *c, char *dst, size_t max)
{
    if (c[0] == '/') { copia_texto(dst, c, max); return; }
    char base[160];
    if (pasta_atual(base, sizeof base) < 0 || !base[0]) copia_texto(base, "/", sizeof base);
    copia_texto(dst, base, max);
    if (compara_texto(dst, "/") != 0) junta_texto(dst, "/", max);
    junta_texto(dst, c, max);
}

/* -------------------------------------------------------------- executar */

static void executar_codigo(void)
{
    if (filho > 0) { mensagem("IDE", "Aguarde a execucao terminar."); return; }
    if (!caminho[0] || area.modificado) if (!salvar()) return;
    int lg = lingua_do_caminho(caminho);
    if (lg == L_NENHUMA) {
        mensagem("IDE", "Use um arquivo .go, .lua, .c ou .mk.");
        return;
    }
    saida_tam = 0; saida[0] = 0;
    gerou_temp = false; rodar_depois = true; executavel[0] = 0;
    aviso[0] = 0;

    if (lg == L_GO) {
        info_arquivo_t info;
        int tentativa;
        for (tentativa = 0; tentativa < 100; tentativa++) {
            formatar_texto(executavel, sizeof executavel, "/tmp/ide-%d-%d", meu_pid(), tentativa);
            if (info_arquivo(executavel, &info) < 0) break;
        }
        if (tentativa == 100) { mensagem("IDE", "Sem nome temporario disponivel em /tmp."); return; }
        gerou_temp = true;
        etapa = 1;
        const char *m = "Compilando Go...\n"; acrescentar(m, (int)tam_texto(m));
        char *args[] = { "gosagui", "build", "-o", executavel, caminho, NULL };
        if (iniciar("/bin/gosagui", args)) return;
    } else if (lg == L_LUA) {
        etapa = 2;
        const char *m = "Interpretando Lua...\n"; acrescentar(m, (int)tam_texto(m));
        char *args[] = { "lua", caminho, NULL };
        if (iniciar("/bin/lua", args)) return;
    } else if (lg == L_C) {
        info_arquivo_t info;
        int tentativa;
        for (tentativa = 0; tentativa < 100; tentativa++) {
            formatar_texto(executavel, sizeof executavel, "/tmp/ide-c-%d-%d", meu_pid(), tentativa);
            if (info_arquivo(executavel, &info) < 0) break;
        }
        if (tentativa == 100) { mensagem("IDE", "Sem nome temporario disponivel em /tmp."); return; }
        gerou_temp = true;
        etapa = 1;
        const char *m = "Compilando C com tinycc...\n"; acrescentar(m, (int)tam_texto(m));
        char *args[] = { "tinycc", "-o", executavel, caminho, "-lsaguigui", NULL };
        if (iniciar("/bin/tinycc", args)) return;
    } else {   /* L_MAKEFILE */
        char mk[200], receita[256];
        bool mk_executa = false;
        caminho_absoluto(caminho, mk, sizeof mk);
        if (!ler_receita_mk(mk, receita, sizeof receita, &mk_executa)) {
            const char *m = "Nao consegui ler o arquivo .mk.\n"; acrescentar(m, (int)tam_texto(m));
            return;
        }
        if (receita[0]) {
            if (receita[0] == '/') copia_texto(executavel, receita, sizeof executavel);
            else formatar_texto(executavel, sizeof executavel, "/tmp/%s", receita);
        }
        rodar_depois = !mk_executa && executavel[0] != 0;
        gerou_temp = rodar_depois && comeca_com(executavel, "/tmp/");
        etapa = 1;
        const char *m = "Compilando pelo make...\n"; acrescentar(m, (int)tam_texto(m));
        char cwd[160];
        if (pasta_atual(cwd, sizeof cwd) < 0) copia_texto(cwd, "/", sizeof cwd);
        mudar_pasta("/tmp");
        char *args[] = { "make", mk, NULL };
        bool ok = iniciar("/bin/make", args);
        mudar_pasta(cwd);
        if (ok) return;
    }

    if (gerou_temp) apagar(executavel);
    gerou_temp = false; etapa = 0;
    const char *m = "Erro ao iniciar ferramenta.\n"; acrescentar(m, (int)tam_texto(m));
}

/* -------------------------------------------------------------- buscar */

static int minusculo(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int buscar_texto(const char *termo, int inicio)
{
    int n = (int)tam_texto(termo);
    if (n <= 0) return -1;
    if (inicio < 0) inicio = 0;
    for (int i = inicio; i + n <= area.tam; i++) {
        int k = 0;
        while (k < n && minusculo((unsigned char)area.buf[i + k]) == minusculo((unsigned char)termo[k])) k++;
        if (k == n) return i;
    }
    return -1;
}

static void localizar(void)
{
    char termo[96];
    copia_texto(termo, ultima_busca, sizeof termo);
    if (!pedir_texto("Localizar", "Texto a localizar:", termo, sizeof termo) || !termo[0])
        return;
    copia_texto(ultima_busca, termo, sizeof ultima_busca);
    int pos = buscar_texto(termo, area.cursor + 1);
    if (pos < 0) pos = buscar_texto(termo, 0);
    if (pos < 0) {
        area.marca_tam = 0;
        copia_texto(aviso, "Nao encontrado", sizeof aviso);
    } else {
        area.cursor = pos;
        area.marca_ini = pos;
        area.marca_tam = (int)tam_texto(termo);
        aviso[0] = 0;
        int vis = area_linhas_visiveis(&area), lin, col;
        posicao_atual(&lin, &col);
        if (lin < area.topo) area.topo = lin;
        if (lin >= area.topo + vis) area.topo = lin - vis + 1;
        if (area.topo < 0) area.topo = 0;
    }
    redesenhar();
}

static void ir_para_linha(void)
{
    char t[24];
    int lin, col;
    posicao_atual(&lin, &col);
    formatar_texto(t, sizeof t, "%d", lin + 1);
    if (!pedir_texto("Ir para linha", "Numero da linha:", t, sizeof t)) return;
    int alvo = texto_para_int(t);
    int total = total_linhas_ide();
    if (alvo < 1) alvo = 1;
    if (alvo > total) alvo = total;
    area.cursor = inicio_da_linha_ide(alvo - 1);
    int vis = area_linhas_visiveis(&area);
    area.topo = alvo - 1 - vis / 2;
    if (area.topo < 0) area.topo = 0;
    if (area.topo > total - vis) area.topo = total - vis > 0 ? total - vis : 0;
    area.marca_tam = 0;
    aviso[0] = 0;
    redesenhar();
}

/* --------------------------------------------------------------- janela */

int principal(int argc, char **argv)
{
    j = jan_abrir("IDE - sem nome", LARG, ALT);
    if (!j || !area_iniciar(&area, 6, BARRA + 4, LARG - 12, STATUS_Y - BARRA - 10)) return 1;

    area.numeros_linha = true;
    area.auto_indentar = true;
    area.cor_token = cor_sintaxe;
    realce.area = &area;
    realce.lingua = lingua;
    realce.linha_cache = NULL;
    realce.bloco_cache = 0;
    area.cor_ctx = &realce;

    botao_t bt[9] = {
        { 8, 7, 70, 26, "Novo", false }, { 86, 7, 70, 26, "Abrir", false },
        { 164, 7, 70, 26, "Salvar", false }, { 242, 7, 104, 26, "Salvar como", false },
        { 354, 7, 90, 26, "Modelos", false },
        { 8, 39, 100, 26, "Localizar", false }, { 116, 39, 110, 26, "Ir p/ linha", false },
        { 244, 39, 110, 26, "Executar", false }, { 362, 39, 90, 26, "Parar", false },
    };
    g_retangulo(j, 0, 0, LARG, BARRA, 0xE6E9EC);
    g_retangulo(j, 0, BARRA - 1, LARG, 1, COR_BORDA);
    rotulo(j, 456, 12, "Ctrl+S salva  Ctrl+F localiza", COR_TEXTO_FRACO);
    rotulo(j, 470, 44, "Ctrl+G vai para linha", COR_TEXTO_FRACO);
    for (int i = 0; i < 9; i++) botao_desenhar(j, &bt[i]);

    area.foco = true;
    if (argc > 1) abrir_arquivo(argv[1]);
    else { forcar_titulo(); redesenhar(); }
    mostrar_saida();
    jan_mostrar(j);
    registrar("ide: pronto\n");

    evento_t e;
    for (;;) {
        acompanhar();
        if (proximo_evento(j->id, &e, false) != 1) { dormir(10); continue; }
        if (e.tipo == EV_FECHAR) {
            if (pode_descartar()) break;
            continue;
        }
        if (e.tipo == EV_TECLA && e.tecla == 19) {          /* Ctrl+S */
            salvar();
            continue;
        }
        if (e.tipo == EV_TECLA && e.tecla == 6) {           /* Ctrl+F */
            localizar();
            continue;
        }
        if (e.tipo == EV_TECLA && e.tecla == 7) {           /* Ctrl+G */
            ir_para_linha();
            continue;
        }
        if (botao_tratar(j, &bt[0], &e) && pode_descartar()) {
            area_definir(&area, "", 0);
            caminho[0] = 0;
            definir_lingua(L_NENHUMA);
            area.marca_tam = 0;
            ultima_busca[0] = 0; aviso[0] = 0;
            forcar_titulo();
            redesenhar();
            continue;
        }
        if (botao_tratar(j, &bt[1], &e) && pode_descartar()) {
            char c[160] = "/";
            if (pedir_texto("Abrir", "Caminho do arquivo:", c, sizeof c)) abrir_arquivo(c);
            continue;
        }
        if (botao_tratar(j, &bt[2], &e)) { salvar(); continue; }
        if (botao_tratar(j, &bt[3], &e)) { salvar_como(); continue; }
        if (botao_tratar(j, &bt[4], &e)) { exemplo_modelo(escolher_modelo()); continue; }
        if (botao_tratar(j, &bt[5], &e)) { localizar(); continue; }
        if (botao_tratar(j, &bt[6], &e)) { ir_para_linha(); continue; }
        if (botao_tratar(j, &bt[7], &e)) { executar_codigo(); continue; }
        if (botao_tratar(j, &bt[8], &e) && filho > 0) {
            etapa = 2; matar(filho);
            const char *m = "\nExecucao interrompida.\n"; acrescentar(m, (int)tam_texto(m));
            continue;
        }
        realce.linha_cache = NULL;
        if (area_tratar(j, &area, &e)) {
            area.marca_tam = 0;
            aviso[0] = 0;
            redesenhar();
        } else if (e.tipo == EV_TECLA || e.tipo == EV_CLIQUE) {
            desenhar_status();   /* setas e cliques mudam a linha/coluna */
        }
        atualizar_titulo();
    }
    registrar("ide: fechado\n");
    if (filho > 0) { matar(filho); int codigo; esperar(filho, &codigo); }
    if (pipe_saida >= 0) fechar(pipe_saida);
    if (gerou_temp) apagar(executavel);
    jan_fechar(j);
    return 0;
}
