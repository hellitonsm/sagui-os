/* ide [arquivo] - editor de codigo baseado no Editor de texto do SaguiOS.
 *
 * Uma barra de botões (Novo, Abrir, Salvar, Salvar como) e a área de texto
 * do kit gráfico. Ctrl+S salva. O título mostra o nome do arquivo e um "*"
 * quando há alteração não salva; fechar com alterações pergunta se quer
 * salvar. O texto é Latin-1, como em todo o sistema. */
#include "sagui.h"
#include "gui.h"

#define LARG 700
#define ALT  480
#define BARRA 72
#define SAIDA_Y 352
#define SAIDA_ALT 122

static jan_t *j;
static area_t area;
static char caminho[160];
static bool modificado_mostrado = true;
static int filho = -1, pipe_saida = -1, etapa = 0, codigo_filho;
static bool terminou = false, gerou_go = false;
static char executavel[80];
static char saida[8192];
static int saida_tam;

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
        if (etapa == 1 && codigo_filho == 0) {
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
        if (gerou_go) apagar(executavel);
        gerou_go = false; etapa = 0;
    }
}


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
    registrar("ide: aberto %s (%d bytes)\n", caminho, n);
    forcar_titulo();
    area_desenhar(j, &area);
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
    registrar("ide: salvo %s (%d bytes)\n", caminho, area.tam);
    forcar_titulo();
    return true;
}

static bool salvar_como(void)
{
    char c[160];
    copia_texto(c, caminho[0] ? caminho : "/tmp/", sizeof c);
    if (!pedir_texto("Salvar como", "Salvar o arquivo em (ex.: /tmp/nota.txt):", c, sizeof c) || !c[0])
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

static void exemplo(const char *texto)
{
    if (!pode_descartar()) return;
    area_definir(&area, texto, (int)tam_texto(texto));
    area.modificado = true; caminho[0] = 0;
    forcar_titulo(); area_desenhar(j, &area);
}

static void executar_codigo(void)
{
    if (filho > 0) { mensagem("IDE", "Aguarde a execucao terminar."); return; }
    if (!caminho[0] || area.modificado) if (!salvar()) return;
    int n = (int)tam_texto(caminho);
    bool go = n >= 3 && compara_texto(caminho + n - 3, ".go") == 0;
    bool lua = n >= 4 && compara_texto(caminho + n - 4, ".lua") == 0;
    if (!go && !lua) { mensagem("IDE", "Use um arquivo .go ou .lua."); return; }
    saida_tam = 0; saida[0] = 0;
    gerou_go = false;
    if (go) {
        info_arquivo_t info;
        int tentativa;
        for (tentativa = 0; tentativa < 100; tentativa++) {
            formatar_texto(executavel, sizeof executavel, "/tmp/ide-%d-%d", meu_pid(), tentativa);
            if (info_arquivo(executavel, &info) < 0) break;
        }
        if (tentativa == 100) { mensagem("IDE", "Sem nome temporario disponivel em /tmp."); return; }
        gerou_go = true;
        etapa = 1;
        const char *m = "Compilando Go...\n"; acrescentar(m, (int)tam_texto(m));
        char *args[] = { "gosagui", "build", "-o", executavel, caminho, NULL };
        if (iniciar("/bin/gosagui", args)) return;
    } else {
        etapa = 2;
        const char *m = "Interpretando Lua...\n"; acrescentar(m, (int)tam_texto(m));
        char *args[] = { "lua", caminho, NULL };
        if (iniciar("/bin/lua", args)) return;
    }
    if (gerou_go) apagar(executavel);
    gerou_go = false; etapa = 0;
    const char *m = "Erro ao iniciar ferramenta.\n"; acrescentar(m, (int)tam_texto(m));
}

int principal(int argc, char **argv)
{
    j = jan_abrir("IDE - sem nome", LARG, ALT);
    if (!j || !area_iniciar(&area, 6, BARRA + 4, LARG - 12, SAIDA_Y - BARRA - 10)) return 1;
    botao_t bt[8] = {
        { 8, 7, 80, 26, "Novo", false }, { 96, 7, 80, 26, "Abrir", false },
        { 184, 7, 80, 26, "Salvar", false }, { 272, 7, 110, 26, "Salvar como", false },
        { 8, 39, 110, 26, "Exemplo Go", false }, { 126, 39, 110, 26, "Exemplo Lua", false },
        { 244, 39, 110, 26, "Executar", false }, { 362, 39, 90, 26, "Parar", false },
    };
    g_retangulo(j, 0, 0, LARG, BARRA, 0xE6E9EC);
    g_retangulo(j, 0, BARRA - 1, LARG, 1, COR_BORDA);
    rotulo(j, 400, 12, "Ctrl+S salva", COR_TEXTO_FRACO);
    for (int i = 0; i < 8; i++) botao_desenhar(j, &bt[i]);
    area.foco = true;
    if (argc > 1) abrir_arquivo(argv[1]);
    else forcar_titulo();
    area_desenhar(j, &area);
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
        if (botao_tratar(j, &bt[4], &e)) exemplo("package main\n\nimport \"fmt\"\n\nfunc main() {\n    fmt.Println(\"Ola, SaguiOS!\")\n}\n");
        if (botao_tratar(j, &bt[5], &e)) exemplo("-- Exemplo Lua\nfor i = 1, 3 do\n    print(\"Ola, SaguiOS!\", i)\nend\n");
        if (botao_tratar(j, &bt[6], &e)) executar_codigo();
        if (botao_tratar(j, &bt[7], &e) && filho > 0) {
            etapa = 2; matar(filho);
            const char *m = "\nExecucao interrompida.\n"; acrescentar(m, (int)tam_texto(m));
        }
        if (botao_tratar(j, &bt[0], &e) && pode_descartar()) {
            area_definir(&area, "", 0);
            caminho[0] = 0;
            forcar_titulo();
            area_desenhar(j, &area);
        }
        if (botao_tratar(j, &bt[1], &e) && pode_descartar()) {
            char c[160] = "/";
            if (pedir_texto("Abrir", "Caminho do arquivo:", c, sizeof c)) abrir_arquivo(c);
        }
        if (botao_tratar(j, &bt[2], &e)) salvar();
        if (botao_tratar(j, &bt[3], &e)) salvar_como();
        area_tratar(j, &area, &e);
        atualizar_titulo();
    }
    registrar("ide: fechado\n");
    if (filho > 0) { matar(filho); int codigo; esperar(filho, &codigo); }
    if (pipe_saida >= 0) fechar(pipe_saida);
    if (gerou_go) apagar(executavel);
    jan_fechar(j);
    return 0;
}
