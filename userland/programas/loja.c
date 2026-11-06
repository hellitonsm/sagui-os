/* loja - loja de aplicativos do SaguiOS.
 *
 * Por enquanto nao ha loja on-line. A loja funciona assim:
 *   - Fonte local: lista os fontes Go de /go e Lua de /lua como apps
 *     instalaveis. Instalar Go compila com /bin/gosagui e empacota em
 *     /store/nome.loj (formato de programa da loja, com cabecalho de texto
 *     e o ELF depois de "---ELF---"); instalar Lua copia o .lua para
 *     /store/nome.lua (roda com lua /store/nome.lua).
 *   - Apps instalados ficam em /store (pasta propria da loja, separada de /bin).
 *   - Loja on-line: o link fica em /config/loja.conf (linha "link=<url>").
 *     Configure com "loja configurar <url>" ou pelo botao "Configuracoes".
 *     "loja atualizar" busca o link com http_obter e lista os apps.
 *
 * Uso: loja [listar|instalar <nome>|executar <nome>|informar <arquivo.loj>|link|configurar <url>|atualizar]
 * Sem argumento: abre a janela.
 */
#include "sagui.h"
#include "gui.h"

#define ARQ_CONFIG "/config/loja.conf"
#define MAX_APPS 32

typedef struct {
    char nome[48];       /* sem extensao, ex: ola */
    char fonte[96];      /* caminho completo do fonte */
    char destino[96];    /* /store/nome.loj (Go) ou /store/nome.lua (Lua) */
    int  tipo;           /* 1=Go, 2=Lua */
    int  instalado;
} app_t;

static app_t apps[MAX_APPS];
static int n_apps;
static char link_loja[160];

static bool termina_com(const char *s, const char *suf)
{
    unsigned n = (unsigned)tam_texto(s), m = (unsigned)tam_texto(suf);
    return n >= m && compara_texto(s + n - m, suf) == 0;
}

static void ler_link(void)
{
    link_loja[0] = 0;
    int fd = abrir(ARQ_CONFIG, ABRIR_LER);
    if (fd < 0) return;
    char linha[192];
    int n = -1;
    for (;;) {
        n = ler_linha(fd, linha, sizeof linha - 1);
        if (n < 0) break;
        linha[n] = 0;
        if (comeca_com(linha, "link=") || acha_char(linha, '=')) break;
    }
    fechar(fd);
    if (n < 0) return;
    /* espera "link=" no inicio ou em alguma posicao */
    char *e = acha_char(linha, '=');
    if (e) copia_texto(link_loja, e + 1, sizeof link_loja);
    int m = (int)tam_texto(link_loja);
    while (m > 0 && (link_loja[m-1] == '\n' || link_loja[m-1] == '\r' || link_loja[m-1] == ' ')) link_loja[--m] = 0;
    /* remove espacos no inicio */
    char *s = link_loja;
    while (*s == ' ') s++;
    if (s != link_loja) copia_texto(link_loja, s, sizeof link_loja);
}

static int salvar_link(const char *url)
{
    int fd = abrir(ARQ_CONFIG, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd < 0) return fd;
    char buf[192];
    formatar_texto(buf, sizeof buf, "link=%s\n", url);
    int r = escrever_tudo(fd, buf, (unsigned)tam_texto(buf));
    fechar(fd);
    if (r < 0) return r;
    copia_texto(link_loja, url, sizeof link_loja);
    return 0;
}

static int copiar_arquivo(const char *origem, const char *destino)
{
    int f1 = abrir(origem, ABRIR_LER);
    if (f1 < 0) return f1;
    int f2 = abrir(destino, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (f2 < 0) { fechar(f1); return f2; }
    char buf[4096];
    int r;
    while ((r = ler(f1, buf, sizeof buf)) > 0)
        if (escrever_tudo(f2, buf, (unsigned)r) < 0) { fechar(f1); fechar(f2); return -1; }
    fechar(f1); fechar(f2);
    return r < 0 ? r : 0;
}

/* Formato .loj (v1): cabecalho de texto "chave=valor" por linha, depois a
 * linha "---ELF---" e os bytes do ELF. Legivel com cat e com a loja. */
static int empacotar_loj(const char *nome, const char *fonte, const char *elf, const char *destino)
{
    info_arquivo_t inf;
    if (info_arquivo(elf, &inf) < 0) return -1;
    int f1 = abrir(elf, ABRIR_LER);
    if (f1 < 0) return f1;
    int f2 = abrir(destino, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (f2 < 0) { fechar(f1); return f2; }
    char cab[320];
    formatar_texto(cab, sizeof cab,
        "formato=SAGUI-LOJA\nversao=1\nnome=%s\ntipo=go\nfonte=%s\ntamanho_elf=%u\n",
        nome, fonte, (unsigned)inf.tamanho);
    if (escrever_tudo(f2, cab, (unsigned)tam_texto(cab)) < 0 ||
        escrever_tudo(f2, "---ELF---\n", 10) < 0) {
        fechar(f1); fechar(f2); apagar(destino); return -1;
    }
    char buf[4096];
    int r;
    while ((r = ler(f1, buf, sizeof buf)) > 0)
        if (escrever_tudo(f2, buf, (unsigned)r) < 0) { fechar(f1); fechar(f2); apagar(destino); return -1; }
    fechar(f1); fechar(f2);
    return r < 0 ? r : 0;
}

static int extrair_loj(const char *origem, const char *destino)
{
    int f1 = abrir(origem, ABRIR_LER);
    if (f1 < 0) return f1;
    char linha[320];
    for (;;) {
        int n = ler_linha(f1, linha, sizeof linha - 1);
        if (n < 0) { fechar(f1); return -1; }
        linha[n] = 0;
        if (compara_texto(linha, "---ELF---") == 0) break;
    }
    int f2 = abrir(destino, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (f2 < 0) { fechar(f1); return f2; }
    char buf[4096];
    int r;
    while ((r = ler(f1, buf, sizeof buf)) > 0)
        if (escrever_tudo(f2, buf, (unsigned)r) < 0) { fechar(f1); fechar(f2); return -1; }
    fechar(f1); fechar(f2);
    return r < 0 ? r : 0;
}

static void carregar_locais(void)
{
    info_arquivo_t inf;
    n_apps = 0;
    entrada_pasta_t e;
    const char *pastas[2] = { "/go", "/lua" };
    for (int k = 0; k < 2 && n_apps < MAX_APPS; k++) {
        for (int i = 0; listar(pastas[k], i, &e) == 1 && n_apps < MAX_APPS; i++) {
            if (e.tipo == TIPO_PASTA) continue;
            const char *nm = e.nome;
            bool is_go = k == 0 && termina_com(nm, ".go");
            bool is_lua = k == 1 && termina_com(nm, ".lua");
            if (!is_go && !is_lua) continue;
            /* nome sem extensao */
            int tam = (int)tam_texto(nm);
            int ext = is_go ? 3 : 4;
            app_t *a = &apps[n_apps];
            copia_texto(a->nome, nm, sizeof a->nome);
            a->nome[tam - ext] = 0;
            formatar_texto(a->fonte, sizeof a->fonte, "%s/%s", pastas[k], nm);
            if (is_go) formatar_texto(a->destino, sizeof a->destino, "/store/%s.loj", a->nome);
            else formatar_texto(a->destino, sizeof a->destino, "/store/%s.lua", a->nome);
            a->tipo = is_go ? 1 : 2;
            a->instalado = info_arquivo(a->destino, &inf) >= 0;
            n_apps++;
        }
    }
}

static int instalar(int idx)
{
    app_t *a = &apps[idx];
    if (a->tipo == 1) {
        imprimir("loja: compilando %s...\n", a->fonte);
        registrar("loja: compilando %s\n", a->fonte);
        char tmp[96];
        formatar_texto(tmp, sizeof tmp, "/tmp/loja-%d", meu_pid());
        char *args[] = { "gosagui", "build", "-o", tmp, a->fonte, NULL };
        int pid = executar("/bin/gosagui", args);
        if (pid < 0) { imprimir_em(ERROS, "loja: nao consegui iniciar gosagui\n"); return 1; }
        int codigo = 0;
        if (esperar(pid, &codigo) < 0 || codigo != 0) {
            imprimir_em(ERROS, "loja: compilacao falhou (%d)\n", codigo);
            apagar(tmp);
            return 1;
        }
        if (empacotar_loj(a->nome, a->fonte, tmp, a->destino) < 0) {
            imprimir_em(ERROS, "loja: nao consegui empacotar %s\n", a->destino);
            apagar(tmp);
            return 1;
        }
        apagar(tmp);
    } else {
        if (copiar_arquivo(a->fonte, a->destino) < 0) {
            imprimir_em(ERROS, "loja: copia falhou para %s\n", a->destino);
            return 1;
        }
        imprimir("loja: copiado %s -> %s\n", a->fonte, a->destino);
    }
    a->instalado = 1;
    imprimir("loja: instalado %s -> %s\n", a->nome, a->destino);
    registrar("loja: instalado %s -> %s\n", a->nome, a->destino);
    return 0;
}

static int informar_cmd(const char *caminho)
{
    int fd = abrir(caminho, ABRIR_LER);
    if (fd < 0) { imprimir_em(ERROS, "loja: %s: %s\n", caminho, nome_erro(fd)); return 1; }
    char linha[320];
    int cab = 0;
    for (;;) {
        int n = ler_linha(fd, linha, sizeof linha - 1);
        if (n < 0) { fechar(fd); imprimir_em(ERROS, "loja: %s nao e um .loj\n", caminho); return 1; }
        linha[n] = 0;
        if (compara_texto(linha, "---ELF---") == 0) break;
        imprimir("loja: %s\n", linha);
        cab++;
        if (cab > 16) break;
    }
    fechar(fd);
    registrar("loja: informar %s\n", caminho);
    return 0;
}

/* Go vem em .loj: extrai o ELF para /tmp e executa. Lua: direto no lua. */
static int abrir_app(app_t *a)
{
    char tmp[96];
    formatar_texto(tmp, sizeof tmp, "/tmp/loja-%d", meu_pid());
    if (a->tipo == 1) {
        if (extrair_loj(a->destino, tmp) < 0) {
            imprimir_em(ERROS, "loja: nao consegui extrair %s\n", a->destino);
            return -1;
        }
        char *args[] = { tmp, NULL };
        int pid = executar(tmp, args);
        registrar("loja: abrindo %s (pid %d)\n", a->nome, pid);
        return pid;
    }
    char *args[] = { "lua", a->destino, NULL };
    int pid = executar("/bin/lua", args);
    registrar("loja: abrindo %s (pid %d)\n", a->nome, pid);
    return pid;
}

/* Roda um app instalado: Go extrai o .loj e executa; Lua roda o script. */
static int executar_cmd(const char *nome)
{
    ler_link(); carregar_locais();
    for (int i = 0; i < n_apps; i++) {
        if (compara_texto(apps[i].nome, nome) != 0) continue;
        if (!apps[i].instalado) {
            imprimir_em(ERROS, "loja: %s nao instalado. Use: loja instalar %s\n", nome, nome);
            return 1;
        }
        char tmp[96];
        formatar_texto(tmp, sizeof tmp, "/tmp/loja-%d", meu_pid());
        if (apps[i].tipo == 1) {
            if (extrair_loj(apps[i].destino, tmp) < 0) {
                imprimir_em(ERROS, "loja: nao consegui extrair %s\n", apps[i].destino);
                return 1;
            }
            char *args[] = { tmp, NULL };
            int pid = executar(tmp, args);
            if (pid < 0) { apagar(tmp); imprimir_em(ERROS, "loja: nao consegui executar %s\n", nome); return 1; }
            int codigo = 0;
            esperar(pid, &codigo);
            apagar(tmp);
            registrar("loja: executado %s (codigo %d)\n", nome, codigo);
            return codigo;
        }
        char *args[] = { "lua", apps[i].destino, NULL };
        int pid = executar("/bin/lua", args);
        if (pid < 0) { imprimir_em(ERROS, "loja: nao consegui executar %s\n", nome); return 1; }
        int codigo = 0;
        esperar(pid, &codigo);
        registrar("loja: executado %s (codigo %d)\n", nome, codigo);
        return codigo;
    }
    imprimir_em(ERROS, "loja: app nao encontrado: %s\n", nome);
    return 1;
}

static int listar_cmd(void)
{
    ler_link();
    carregar_locais();
    imprimir("loja: fonte local (/go e /lua): %d apps\n", n_apps);
    for (int i = 0; i < n_apps; i++)
        imprimir("loja: %s (%s) %s\n", apps[i].nome, apps[i].tipo == 1 ? "Go" : "Lua",
                 apps[i].instalado ? "instalado" : "nao instalado");
    if (link_loja[0]) imprimir("loja: link configurado: %s\n", link_loja);
    else imprimir("loja: sem link de loja on-line (configure com loja configurar <url>)\n");
    registrar("loja: listar %d apps\n", n_apps);
    return 0;
}

static int atualizar_cmd(void)
{
    ler_link();
    if (!link_loja[0]) {
        imprimir_em(ERROS, "loja: sem link configurado. Use: loja configurar <url>\n");
        return 1;
    }
    imprimir("loja: buscando %s ...\n", link_loja);
    registrar("loja: atualizar %s\n", link_loja);
    char *buf = aloca(65536);
    if (!buf) { imprimir_em(ERROS, "loja: sem memoria\n"); return 1; }
    int n = http_obter(link_loja, buf, 65536);
    if (n < 0) { imprimir_em(ERROS, "loja: falha de rede/HTTP (%d).\n", n); libera(buf); return 1; }
    /* lista cada linha "nome url" como app remoto (apenas mostra) */
    buf[n < 65535 ? n : 65535] = 0;
    char *p = buf;
    int remotos = 0;
    while (*p) {
        char *eol = acha_char(p, '\n');
        if (eol) *eol = 0;
        if (p[0] && p[0] != '#') { imprimir("loja: remoto: %s\n", p); remotos++; }
        if (!eol) break;
        p = eol + 1;
    }
    libera(buf);
    imprimir("loja: %d apps remotos\n", remotos);
    return 0;
}

/* ---- janela ---- */
#define LLARG 680
#define LALT 480

static jan_t *j;
static char txt_lista[MAX_APPS][64];

static void redesenhar_lista(lista_t *lista)
{
    for (int i = 0; i < n_apps; i++)
        formatar_texto(txt_lista[i], sizeof txt_lista[i], "%s (%s) %s",
                       apps[i].nome, apps[i].tipo == 1 ? "Go" : "Lua",
                       apps[i].instalado ? "instalado" : "nao instalado");
    lista->itens = txt_lista;
    lista->qtd = n_apps;
    if (lista->sel >= n_apps) lista->sel = n_apps ? n_apps-1 : -1;
    lista_desenhar(j, lista);
}

int principal(int argc, char **argv)
{
    if (argc >= 2) {
        if (compara_texto(argv[1], "listar") == 0) return listar_cmd();
        if (compara_texto(argv[1], "atualizar") == 0) return atualizar_cmd();
        if (compara_texto(argv[1], "informar") == 0) {
            if (argc != 3) { imprimir_em(ERROS, "uso: loja informar arquivo.loj\n"); return 1; }
            return informar_cmd(argv[2]);
        }
        if (compara_texto(argv[1], "link") == 0) {
            ler_link();
            imprimir("loja: %s\n", link_loja[0] ? link_loja : "(sem link)");
            registrar("loja: link %s\n", link_loja[0] ? link_loja : "vazio");
            return 0;
        }
        if (compara_texto(argv[1], "configurar") == 0) {
            if (argc != 3) { imprimir_em(ERROS, "uso: loja configurar http://loja.exemplo/\n"); return 1; }
            if (salvar_link(argv[2]) < 0) { imprimir_em(ERROS, "loja: nao consegui salvar configuracao\n"); return 1; }
            imprimir("loja: link salvo: %s\n", argv[2]);
            registrar("loja: link salvo %s\n", argv[2]);
            return 0;
        }
        if (compara_texto(argv[1], "instalar") == 0) {
            if (argc != 3) { imprimir_em(ERROS, "uso: loja instalar <nome>\n"); return 1; }
            ler_link(); carregar_locais();
            for (int i = 0; i < n_apps; i++)
                if (compara_texto(apps[i].nome, argv[2]) == 0) return instalar(i);
            imprimir_em(ERROS, "loja: app nao encontrado: %s\n", argv[2]);
            return 1;
        }
        if (compara_texto(argv[1], "executar") == 0) {
            if (argc != 3) { imprimir_em(ERROS, "uso: loja executar <nome>\n"); return 1; }
            return executar_cmd(argv[2]);
        }
        if (compara_texto(argv[1], "-h") == 0 || compara_texto(argv[1], "--ajuda") == 0) {
            imprimir("uso: loja [listar|instalar <nome>|executar <nome>|informar <arquivo.loj>|link|configurar <url>|atualizar]\n");
            return 0;
        }
        imprimir_em(ERROS, "uso: loja [listar|instalar <nome>|executar <nome>|informar <arquivo.loj>|link|configurar <url>|atualizar]\n");
        return 1;
    }

    ler_link();
    carregar_locais();
    j = jan_abrir("Loja de Apps", LLARG, LALT);
    if (!j) return 1;
    botao_t bt_inst = { 8, 8, 90, 26, "Instalar", false };
    botao_t bt_abrir = { 106, 8, 90, 26, "Abrir", false };
    botao_t bt_atual = { 204, 8, 110, 26, "Atualizar", false };
    botao_t bt_conf = { 322, 8, 130, 26, "Configuracoes", false };
    botao_t bt_fechar = { 460, 8, 90, 26, "Fechar", false };
    botao_desenhar(j, &bt_inst); botao_desenhar(j, &bt_abrir); botao_desenhar(j, &bt_atual);
    botao_desenhar(j, &bt_conf); botao_desenhar(j, &bt_fechar);
    rotulo(j, 8, 40, "Fonte local: /go e /lua  |  Online: link em /config/loja.conf", COR_TEXTO_FRACO);
    lista_t lista = { 8, 62, LLARG - 16, 340, txt_lista, n_apps, n_apps ? 0 : -1, 0, NULL };
    redesenhar_lista(&lista);
    char rod[200];
    if (link_loja[0]) formatar_texto(rod, sizeof rod, "Loja on-line: %s  |  %d apps locais.", link_loja, n_apps);
    else formatar_texto(rod, sizeof rod, "Sem loja on-line. Configure o link em Configuracoes.  |  %d apps locais.", n_apps);
    rotulo(j, 8, LALT - 30, rod, COR_TEXTO_FRACO);
    jan_mostrar(j);
    registrar("loja: pronto\n");
    evento_t e;
    for (;;) {
        if (proximo_evento(j->id, &e, false) != 1) { dormir(20); continue; }
        if (e.tipo == EV_FECHAR) break;
        if (botao_tratar(j, &bt_fechar, &e)) break;
        if (botao_tratar(j, &bt_conf, &e)) {
            char url[160];
            copia_texto(url, link_loja, sizeof url);
            if (pedir_texto("Loja on-line", "Link da loja (http://...):", url, sizeof url)) {
                if (salvar_link(url) < 0) mensagem("Loja", "Nao consegui salvar.");
                else {
                    char m[200];
                    formatar_texto(m, sizeof m, link_loja[0] ? "Link: %s" : "Link apagado.", link_loja);
                    registrar("loja: link salvo %s\n", link_loja);
                    rotulo(j, 8, LALT - 30, m, COR_TEXTO_FRACO);
                    jan_mostrar_area(j, 8, LALT - 30, LLARG - 16, 16);
                }
            }
        }
        if (botao_tratar(j, &bt_atual, &e)) {
            ler_link();
            if (!link_loja[0]) mensagem("Loja", "Sem link.\nUse Configuracoes.");
            else { atualizar_cmd(); mensagem("Loja", "Busca feita.\nVeja a saida; lista remota no log."); }
        }
        if (botao_tratar(j, &bt_inst, &e) && lista.sel >= 0 && lista.sel < n_apps) {
            if (instalar(lista.sel) == 0) { carregar_locais(); redesenhar_lista(&lista); }
            else mensagem("Loja", "Nao consegui instalar.");
        }
        if (botao_tratar(j, &bt_abrir, &e) && lista.sel >= 0 && lista.sel < n_apps && apps[lista.sel].instalado) {
            abrir_app(&apps[lista.sel]);
        }
        int r = lista_tratar(j, &lista, &e);
        if (r == LISTA_ATIVOU && lista.sel >= 0 && lista.sel < n_apps && apps[lista.sel].instalado) {
            abrir_app(&apps[lista.sel]);
        }
    }
    registrar("loja: fechada\n");
    jan_fechar(j);
    return 0;
}
