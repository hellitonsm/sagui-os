/* ldagui - ligador estatico ELF32 i386 para objetos relocaveis do SaguiOS.
 * Implementa o subconjunto de ELF32/SHT_REL necessario para objetos C sem PIC. */
#include "sagui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define OBJETOS_MAX       32u
#define ARQUIVO_MAX       (16u * 1024u * 1024u)
#define SECOES_MAX        512u
#define SIMBOLOS_OBJ_MAX  262144u
#define IMAGEM_MAX        (48u * 1024u * 1024u)
#define PAGINA_ELF        4096u
#define BASE_ELF          0x40000000u
#define INVALIDO          0xFFFFFFFFu

#define ET_REL            1u
#define EM_386            3u
#define SHT_PROGBITS      1u
#define SHT_SYMTAB        2u
#define SHT_STRTAB        3u
#define SHT_RELA           4u
#define SHT_NOBITS        8u
#define SHT_REL           9u
#define SHF_ALLOC         2u
#define SHF_TLS           0x400u
#define PT_LOAD           1u
#define PT_TLS            7u
#define SHN_UNDEF         0u
#define SHN_ABS           0xFFF1u
#define SHN_COMMON        0xFFF2u
#define STB_LOCAL         0u
#define STB_GLOBAL        1u
#define STB_WEAK          2u
#define R_386_NONE        0u
#define R_386_32          1u
#define R_386_PC32        2u
#define R_386_PLT32       4u
#define R_386_TLS_TPOFF  14u
#define R_386_TLS_LE     17u

typedef struct {
    unsigned name_off, type, flags, addr, off, size, link, info, align, entsize;
    unsigned out_off;
    const char *name;
} sec_t;

typedef struct {
    const char *name;
    unsigned value, size, info, other, shndx;
} sym_t;

typedef struct {
    const char *path;
    unsigned char *data;
    unsigned size;
    unsigned type, machine, shoff, shnum, shentsize, shstrndx;
    sec_t *secs;
    sym_t *syms;
    unsigned nsym, symtab_index, strtab_index;
} obj_t;

typedef struct {
    const char *name;
    unsigned obj, sym;
    unsigned state; /* 1=forte definido, 2=fraco definido */
} global_t;

typedef struct { unsigned obj, sec; } sec_ref_t;

static obj_t objetos[OBJETOS_MAX];
static unsigned n_objetos;
static global_t *globais;
static unsigned capacidade_globais;
static unsigned char *imagem;
static unsigned tamanho_imagem;
static unsigned erro;
static sec_ref_t refs_secoes[OBJETOS_MAX * SECOES_MAX];
static unsigned n_refs_secoes;
static unsigned inicio_preinit, fim_preinit, inicio_init, fim_init, inicio_fini, fim_fini;
static unsigned tls_inicio, tls_filesz, tls_memsz, tls_alinhamento, tls_tp_offset;

static unsigned le16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}
static unsigned le32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
static void grava16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
}
static void grava32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}
static int faixa(unsigned total, unsigned off, unsigned n)
{
    return off <= total && n <= total - off;
}
static unsigned alinhar(unsigned n, unsigned a)
{
    if (a <= 1) return n;
    if ((a & (a - 1u)) != 0 || a > PAGINA_ELF) return INVALIDO;
    if (n > 0xFFFFFFFFu - (a - 1u)) return INVALIDO;
    return (n + a - 1u) & ~(a - 1u);
}
static const char *string_na_tabela(const unsigned char *base, unsigned tamanho, unsigned off)
{
    if (off >= tamanho) return NULL;
    const char *s = (const char *)(base + off);
    for (unsigned i = off; i < tamanho; i++) if (base[i] == 0) return s;
    return NULL;
}
static void falhar(const char *path, const char *mensagem)
{
    if (!erro) fprintf(stderr, "ldagui: %s: %s\n", path ? path : "link", mensagem);
    erro = 1;
}

static unsigned char *ler_arquivo(const char *path, unsigned *tamanho)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "ldagui: %s: %s\n", path, strerror(errno)); return NULL; }
    unsigned cap = 16384, usado = 0;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) { close(fd); fprintf(stderr, "ldagui: memoria insuficiente\n"); return NULL; }
    for (;;) {
        if (usado == cap) {
            if (cap >= ARQUIVO_MAX) {
                unsigned char extra;
                ssize_t x = read(fd, &extra, 1);
                if (x > 0) { free(buf); close(fd); fprintf(stderr, "ldagui: %s: objeto excede 16 MiB\n", path); return NULL; }
                if (x < 0) { free(buf); close(fd); fprintf(stderr, "ldagui: %s: erro de leitura\n", path); return NULL; }
                break;
            }
            unsigned novo = cap * 2u;
            if (novo > ARQUIVO_MAX) novo = ARQUIVO_MAX;
            unsigned char *p = (unsigned char *)realloc(buf, novo);
            if (!p) { free(buf); close(fd); fprintf(stderr, "ldagui: memoria insuficiente\n"); return NULL; }
            buf = p; cap = novo;
        }
        ssize_t n = read(fd, buf + usado, cap - usado);
        if (n < 0) { free(buf); close(fd); fprintf(stderr, "ldagui: %s: erro de leitura\n", path); return NULL; }
        if (!n) break;
        usado += (unsigned)n;
    }
    close(fd);
    *tamanho = usado;
    return buf;
}

static int carregar_objeto(obj_t *o, const char *path)
{
    memset(o, 0, sizeof *o);
    o->path = path;
    o->data = ler_arquivo(path, &o->size);
    if (!o->data) return 0;
    if (o->size < 52 || memcmp(o->data, "\177ELF", 4) ||
        o->data[4] != 1 || o->data[5] != 1 || o->data[6] != 1) {
        falhar(path, "esperado ELF32 little-endian"); return 0;
    }
    o->type = le16(o->data + 16);
    o->machine = le16(o->data + 18);
    if (o->type != ET_REL || o->machine != EM_386) {
        falhar(path, "esperado objeto ELF32 relocavel i386 (ET_REL)"); return 0;
    }
    o->shoff = le32(o->data + 32);
    o->shentsize = le16(o->data + 46);
    o->shnum = le16(o->data + 48);
    o->shstrndx = le16(o->data + 50);
    if (o->shentsize != 40 || !o->shnum || o->shnum > SECOES_MAX ||
        o->shstrndx >= o->shnum || !faixa(o->size, o->shoff, o->shnum * 40u)) {
        falhar(path, "tabela ELF de secoes invalida"); return 0;
    }
    o->secs = (sec_t *)calloc(o->shnum, sizeof(sec_t));
    if (!o->secs) { falhar(path, "memoria insuficiente para secoes"); return 0; }
    for (unsigned i = 0; i < o->shnum; i++) {
        const unsigned char *h = o->data + o->shoff + i * 40u;
        sec_t *s = &o->secs[i];
        s->name_off = le32(h); s->type = le32(h + 4); s->flags = le32(h + 8);
        s->addr = le32(h + 12); s->off = le32(h + 16); s->size = le32(h + 20);
        s->link = le32(h + 24); s->info = le32(h + 28); s->align = le32(h + 32);
        s->entsize = le32(h + 36); s->out_off = INVALIDO;
        if (s->type != SHT_NOBITS && s->size && !faixa(o->size, s->off, s->size)) {
            falhar(path, "secao aponta para fora do arquivo"); return 0;
        }
    }
    sec_t *names = &o->secs[o->shstrndx];
    if (names->type != SHT_STRTAB || !faixa(o->size, names->off, names->size)) {
        falhar(path, "string table de secoes invalida"); return 0;
    }
    for (unsigned i = 0; i < o->shnum; i++) {
        o->secs[i].name = string_na_tabela(o->data + names->off, names->size, o->secs[i].name_off);
        if (!o->secs[i].name) { falhar(path, "nome de secao invalido"); return 0; }
        if (o->secs[i].type == SHT_SYMTAB) {
            if (o->symtab_index) { falhar(path, "mais de uma .symtab nao suportada"); return 0; }
            o->symtab_index = i;
        }
    }
    if (!o->symtab_index) { falhar(path, "objeto sem .symtab"); return 0; }
    sec_t *symtab = &o->secs[o->symtab_index];
    if (symtab->entsize != 16 || symtab->size % 16 || symtab->link >= o->shnum) {
        falhar(path, ".symtab invalida"); return 0;
    }
    o->strtab_index = symtab->link;
    sec_t *strtab = &o->secs[o->strtab_index];
    if (strtab->type != SHT_STRTAB || !faixa(o->size, strtab->off, strtab->size)) {
        falhar(path, ".strtab invalida"); return 0;
    }
    o->nsym = symtab->size / 16u;
    if (!o->nsym || o->nsym > SIMBOLOS_OBJ_MAX) { falhar(path, "quantidade de simbolos fora do limite"); return 0; }
    o->syms = (sym_t *)calloc(o->nsym, sizeof(sym_t));
    if (!o->syms) { falhar(path, "memoria insuficiente para simbolos"); return 0; }
    for (unsigned i = 0; i < o->nsym; i++) {
        const unsigned char *p = o->data + symtab->off + i * 16u;
        sym_t *s = &o->syms[i];
        unsigned no = le32(p);
        s->value = le32(p + 4); s->size = le32(p + 8);
        s->info = p[12]; s->other = p[13]; s->shndx = le16(p + 14);
        s->name = string_na_tabela(o->data + strtab->off, strtab->size, no);
        if (!s->name) { falhar(path, "nome de simbolo invalido"); return 0; }
        if (s->shndx < o->shnum && s->shndx != SHN_UNDEF &&
            s->value > o->secs[s->shndx].size && o->secs[s->shndx].type != SHT_NOBITS) {
            falhar(path, "valor de simbolo fora da secao"); return 0;
        }
    }
    return 1;
}

static unsigned hash_nome(const char *s)
{
    unsigned h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}
static int igual_nome(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}
static unsigned global_slot(const char *nome)
{
    unsigned m = capacidade_globais - 1u;
    unsigned i = hash_nome(nome) & m;
    while (globais[i].state && !igual_nome(globais[i].name, nome)) i = (i + 1u) & m;
    return i;
}
static int incluir_global(const char *nome, unsigned oi, unsigned si, unsigned fraco)
{
    if (!nome || !*nome) return 1;
    unsigned i = global_slot(nome);
    unsigned state = fraco ? 2u : 1u;
    if (!globais[i].state) {
        globais[i].name = nome; globais[i].obj = oi; globais[i].sym = si; globais[i].state = state;
        return 1;
    }
    if (globais[i].state == 1u && state == 1u) {
        fprintf(stderr, "ldagui: simbolo global duplicado: %s\n", nome); erro = 1; return 0;
    }
    if (globais[i].state == 2u && state == 1u) {
        globais[i].obj = oi; globais[i].sym = si; globais[i].state = 1u;
    }
    return 1;
}
static global_t *procurar_global(const char *nome)
{
    if (!globais || !nome || !*nome) return NULL;
    unsigned i = global_slot(nome);
    return globais[i].state ? &globais[i] : NULL;
}
static int simbolo_definido(const sym_t *s)
{
    return s->shndx != SHN_UNDEF;
}

static int preparar_globais(void)
{
    unsigned total = 0;
    for (unsigned oi = 0; oi < n_objetos; oi++) {
        if (objetos[oi].nsym > 0x7FFFFFFFu - total) { falhar(objetos[oi].path, "tabela de simbolos excede limite"); return 0; }
        total += objetos[oi].nsym;
    }
    unsigned cap = 16;
    while (cap < total * 2u && cap < 0x40000000u) cap <<= 1;
    if (cap < total || cap > (1u << 24)) { falhar(NULL, "tabela global excede limite"); return 0; }
    capacidade_globais = cap;
    globais = (global_t *)calloc(capacidade_globais, sizeof(global_t));
    if (!globais) { falhar(NULL, "memoria insuficiente para tabela global"); return 0; }
    for (unsigned oi = 0; oi < n_objetos && !erro; oi++) {
        obj_t *o = &objetos[oi];
        for (unsigned si = 1; si < o->nsym; si++) {
            sym_t *s = &o->syms[si];
            unsigned bind = s->info >> 4;
            if ((bind == STB_GLOBAL || bind == STB_WEAK) && simbolo_definido(s)) {
                if (s->shndx == SHN_COMMON) { falhar(o->path, "SHN_COMMON nao suportado; compile com -fno-common"); return 0; }
                if (!incluir_global(s->name, oi, si, bind == STB_WEAK)) return 0;
            }
        }
    }
    return !erro;
}

static int simbolo_sintetico(const char *nome, unsigned *valor)
{
    unsigned base = BASE_ELF + PAGINA_ELF;
    if (!strcmp(nome, "__ehdr_start") || !strcmp(nome, "__executable_start")) *valor = BASE_ELF;
    else if (!strcmp(nome, "__preinit_array_start")) *valor = base + inicio_preinit;
    else if (!strcmp(nome, "__preinit_array_end")) *valor = base + fim_preinit;
    else if (!strcmp(nome, "__init_array_start")) *valor = base + inicio_init;
    else if (!strcmp(nome, "__init_array_end")) *valor = base + fim_init;
    else if (!strcmp(nome, "__fini_array_start")) *valor = base + inicio_fini;
    else if (!strcmp(nome, "__fini_array_end")) *valor = base + fim_fini;
    else return 0;
    return 1;
}

static int endereco_simbolo(unsigned oi, unsigned si, unsigned *endereco, int *fraco_ausente)
{
    obj_t *o = &objetos[oi];
    if (si >= o->nsym) { falhar(o->path, "relocation referencia indice de simbolo invalido"); return 0; }
    sym_t *s = &o->syms[si];
    unsigned bind = s->info >> 4;
    *fraco_ausente = 0;
    if (s->shndx == SHN_UNDEF) {
        global_t *g = procurar_global(s->name);
        if (!g) {
            if (simbolo_sintetico(s->name, endereco)) return 1;
            if (bind == STB_WEAK) { *endereco = 0; *fraco_ausente = 1; return 1; }
            fprintf(stderr, "ldagui: simbolo indefinido: %s (em %s)\n", s->name, o->path); erro = 1; return 0;
        }
        o = &objetos[g->obj];
        s = &o->syms[g->sym];
    }
    if (s->shndx == SHN_ABS) { *endereco = s->value; return 1; }
    if (s->shndx == SHN_COMMON) { falhar(o->path, "simbolo comum nao suportado"); return 0; }
    if (s->shndx >= o->shnum) { falhar(o->path, "simbolo aponta para secao invalida"); return 0; }
    sec_t *sec = &o->secs[s->shndx];
    if (!(sec->flags & SHF_ALLOC) || sec->out_off == INVALIDO) {
        falhar(o->path, "simbolo definido em secao nao carregavel"); return 0;
    }
    if (s->value > sec->size || sec->out_off > tamanho_imagem - s->value) {
        falhar(o->path, "simbolo fora dos limites da imagem"); return 0;
    }
    *endereco = BASE_ELF + PAGINA_ELF + sec->out_off + s->value;
    return 1;
}

static int endereco_tls_simbolo(unsigned oi, unsigned si, unsigned *offset)
{
    obj_t *o = &objetos[oi];
    if (si >= o->nsym) { falhar(o->path, "relocation TLS referencia simbolo invalido"); return 0; }
    sym_t *s = &o->syms[si];
    unsigned bind = s->info >> 4;
    if (s->shndx == SHN_UNDEF) {
        global_t *g = procurar_global(s->name);
        if (!g) {
            if (bind == STB_WEAK) { *offset = 0; return 1; }
            fprintf(stderr, "ldagui: simbolo TLS indefinido: %s (em %s)\n", s->name, o->path); erro = 1; return 0;
        }
        o = &objetos[g->obj];
        s = &o->syms[g->sym];
    }
    if (s->shndx >= o->shnum || s->shndx == SHN_UNDEF) {
        falhar(o->path, "simbolo TLS aponta para secao invalida"); return 0;
    }
    sec_t *sec = &o->secs[s->shndx];
    if (!(sec->flags & SHF_TLS) || sec->out_off == INVALIDO || sec->out_off < tls_inicio ||
        s->value > sec->size || sec->out_off - tls_inicio > tls_memsz ||
        s->value > tls_memsz - (sec->out_off - tls_inicio)) {
        falhar(o->path, "simbolo definido fora do bloco TLS"); return 0;
    }
    *offset = sec->out_off - tls_inicio + s->value;
    return 1;
}

static int prefixo_secao(const char *nome, const char *prefixo)
{
    unsigned n = (unsigned)strlen(prefixo);
    return !strncmp(nome, prefixo, n) && (nome[n] == 0 || nome[n] == '.');
}

static unsigned categoria_array(const sec_t *s)
{
    if (!(s->flags & SHF_ALLOC) || !s->name) return 0;
    if (prefixo_secao(s->name, ".preinit_array")) return 1;
    if (prefixo_secao(s->name, ".init_array")) return 2;
    if (prefixo_secao(s->name, ".fini_array")) return 3;
    return 0;
}

static unsigned prioridade_array(const char *nome, unsigned grupo)
{
    const char *prefixo = grupo == 1 ? ".preinit_array" :
                          grupo == 2 ? ".init_array" : ".fini_array";
    unsigned n = (unsigned)strlen(prefixo);
    if (!strcmp(nome, prefixo)) return 0xFFFFFFFFu; /* sem sufixo: prioridade padrão, por último */
    if (strncmp(nome, prefixo, n) || nome[n] != '.') return 0xFFFFFFFEu;
    const char *p = nome + n + 1;
    if (*p < '0' || *p > '9') return 0xFFFFFFFEu;
    unsigned v = 0;
    while (*p >= '0' && *p <= '9') {
        unsigned d = (unsigned)(*p++ - '0');
        if (v > (0xFFFFFFFDu - d) / 10u) return 0xFFFFFFFDu;
        v = v * 10u + d;
    }
    return v;
}

static unsigned ordem_array;
static int comparar_refs(const void *pa, const void *pb)
{
    const sec_ref_t *a = (const sec_ref_t *)pa, *b = (const sec_ref_t *)pb;
    sec_t *sa = &objetos[a->obj].secs[a->sec], *sb = &objetos[b->obj].secs[b->sec];
    unsigned ap = prioridade_array(sa->name, ordem_array), bp = prioridade_array(sb->name, ordem_array);
    if (ap != bp) return ap < bp ? -1 : 1;
    int r = strcmp(sa->name, sb->name);
    if (r) return r;
    if (a->obj != b->obj) return a->obj < b->obj ? -1 : 1;
    if (a->sec != b->sec) return a->sec < b->sec ? -1 : 1;
    return 0;
}

static int reservar_secao(unsigned *total, obj_t *o, sec_t *s)
{
    unsigned a = alinhar(*total, s->align ? s->align : 1u);
    if (a == INVALIDO || a > IMAGEM_MAX || s->size > IMAGEM_MAX - a) {
        falhar(o->path, "imagem final excede limite ou alinhamento invalido");
        return 0;
    }
    s->out_off = a;
    *total = a + s->size;
    return 1;
}

static int organizar_arrays(unsigned *total, unsigned grupo, unsigned *inicio, unsigned *fim)
{
    n_refs_secoes = 0;
    for (unsigned oi = 0; oi < n_objetos; oi++) {
        obj_t *o = &objetos[oi];
        for (unsigned si = 1; si < o->shnum; si++) {
            sec_t *s = &o->secs[si];
            if (categoria_array(s) != grupo) continue;
            if (n_refs_secoes == sizeof refs_secoes / sizeof refs_secoes[0]) {
                falhar(o->path, "muitas secoes de inicializacao"); return 0;
            }
            refs_secoes[n_refs_secoes++] = (sec_ref_t){oi, si};
        }
    }
    ordem_array = grupo;
    qsort(refs_secoes, n_refs_secoes, sizeof refs_secoes[0], comparar_refs);
    *inicio = *total;
    for (unsigned i = 0; i < n_refs_secoes; i++) {
        sec_ref_t r = refs_secoes[i];
        if (!reservar_secao(total, &objetos[r.obj], &objetos[r.obj].secs[r.sec])) return 0;
    }
    *fim = *total;
    return 1;
}

static int organizar_tls(unsigned *total)
{
    tls_alinhamento = 1;
    int existe = 0;
    for (unsigned oi = 0; oi < n_objetos; oi++) {
        obj_t *o = &objetos[oi];
        for (unsigned si = 1; si < o->shnum; si++) {
            sec_t *s = &o->secs[si];
            if (!(s->flags & SHF_ALLOC) || !(s->flags & SHF_TLS)) continue;
            existe = 1;
            if (s->type != SHT_PROGBITS && s->type != SHT_NOBITS) {
                falhar(o->path, "tipo de secao TLS nao suportado"); return 0;
            }
            unsigned a = s->align ? s->align : 1u;
            if (a > tls_alinhamento) tls_alinhamento = a;
        }
    }
    tls_inicio = *total;
    tls_filesz = tls_memsz = tls_tp_offset = 0;
    if (!existe) return 1;
    tls_inicio = alinhar(*total, tls_alinhamento);
    if (tls_inicio == INVALIDO || tls_inicio > IMAGEM_MAX) {
        falhar(NULL, "alinhamento TLS excede limite"); return 0;
    }
    *total = tls_inicio;
    /* O modelo i386 coloca os dados inicializados antes do .tbss e o TCB no fim. */
    for (unsigned fase = 0; fase < 2; fase++) {
        for (unsigned oi = 0; oi < n_objetos; oi++) {
            obj_t *o = &objetos[oi];
            for (unsigned si = 1; si < o->shnum; si++) {
                sec_t *s = &o->secs[si];
                if (!(s->flags & SHF_ALLOC) || !(s->flags & SHF_TLS)) continue;
                if ((s->type == SHT_NOBITS) != (fase == 1)) continue;
                if (!reservar_secao(total, o, s)) return 0;
            }
        }
        if (fase == 0) tls_filesz = *total - tls_inicio;
    }
    tls_memsz = *total - tls_inicio;
    unsigned alinhado = alinhar(tls_memsz, tls_alinhamento);
    if (alinhado == INVALIDO) { falhar(NULL, "bloco TLS grande demais"); return 0; }
    tls_tp_offset = 0u - alinhado;
    return 1;
}

static int organizar_secoes(void)
{
    unsigned total = 0;
    for (unsigned oi = 0; oi < n_objetos; oi++) {
        obj_t *o = &objetos[oi];
        for (unsigned si = 1; si < o->shnum; si++) {
            sec_t *s = &o->secs[si];
            if (!(s->flags & SHF_ALLOC) || (s->flags & SHF_TLS) || categoria_array(s)) continue;
            if (!reservar_secao(&total, o, s)) return 0;
        }
    }
    if (!organizar_arrays(&total, 1, &inicio_preinit, &fim_preinit) ||
        !organizar_arrays(&total, 2, &inicio_init, &fim_init) ||
        !organizar_arrays(&total, 3, &inicio_fini, &fim_fini) ||
        !organizar_tls(&total)) return 0;
    if (!total || total > IMAGEM_MAX) { falhar(NULL, "imagem vazia ou grande demais"); return 0; }
    tamanho_imagem = total;
    imagem = (unsigned char *)calloc(1, tamanho_imagem);
    if (!imagem) { falhar(NULL, "memoria insuficiente para imagem final"); return 0; }
    for (unsigned oi = 0; oi < n_objetos; oi++) {
        obj_t *o = &objetos[oi];
        for (unsigned si = 1; si < o->shnum; si++) {
            sec_t *s = &o->secs[si];
            if (!(s->flags & SHF_ALLOC) || s->out_off == INVALIDO || s->type == SHT_NOBITS || !s->size) continue;
            memcpy(imagem + s->out_off, o->data + s->off, s->size);
        }
    }
    return 1;
}

static int aplicar_relocations(void)
{
    for (unsigned oi = 0; oi < n_objetos && !erro; oi++) {
        obj_t *o = &objetos[oi];
        for (unsigned ri = 1; ri < o->shnum && !erro; ri++) {
            sec_t *rel = &o->secs[ri];
            if (rel->type == SHT_RELA) { falhar(o->path, "SHT_RELA nao suportado em i386"); return 0; }
            if (rel->type != SHT_REL) continue;
            if (rel->info >= o->shnum || rel->link != o->symtab_index ||
                rel->entsize != 8 || rel->size % 8) {
                falhar(o->path, "secao de relocations invalida"); return 0;
            }
            sec_t *target = &o->secs[rel->info];
            if (!(target->flags & SHF_ALLOC)) continue;
            if (target->out_off == INVALIDO) { falhar(o->path, "secao alvo nao foi mapeada"); return 0; }
            for (unsigned n = 0; n < rel->size / 8u; n++) {
                const unsigned char *r = o->data + rel->off + n * 8u;
                unsigned roff = le32(r), info = le32(r + 4);
                unsigned symidx = info >> 8, type = info & 0xFFu;
                if (type == R_386_NONE) continue;
                int reloc_tls = type == R_386_TLS_LE || type == R_386_TLS_TPOFF;
                if (type != R_386_32 && type != R_386_PC32 && type != R_386_PLT32 && !reloc_tls) {
                    fprintf(stderr, "ldagui: %s: relocation i386 tipo %u nao suportada\n", o->path, type); erro = 1; return 0;
                }
                if (roff > target->size || target->size - roff < 4 ||
                    target->out_off > tamanho_imagem || target->out_off + roff > tamanho_imagem - 4) {
                    falhar(o->path, "offset de relocation fora da secao"); return 0;
                }
                unsigned S; int fraco = 0;
                if (reloc_tls) {
                    if (!tls_memsz) { falhar(o->path, "relocation TLS sem segmento TLS"); return 0; }
                    if (!endereco_tls_simbolo(oi, symidx, &S)) return 0;
                } else if (!endereco_simbolo(oi, symidx, &S, &fraco)) return 0;
                unsigned loc = target->out_off + roff;
                unsigned A = le32(imagem + loc);
                unsigned P = BASE_ELF + PAGINA_ELF + loc;
                unsigned resultado = reloc_tls ? S + A + tls_tp_offset :
                                     type == R_386_32 ? S + A : S + A - P;
                grava32(imagem + loc, resultado);
                (void)fraco;
            }
        }
    }
    return !erro;
}

static int procurar_entrada(const char *nome, unsigned *offset)
{
    global_t *g = procurar_global(nome);
    if (!g) return 0;
    obj_t *o = &objetos[g->obj]; sym_t *s = &o->syms[g->sym];
    if (s->shndx >= o->shnum || !(o->secs[s->shndx].flags & SHF_ALLOC)) return 0;
    sec_t *sec = &o->secs[s->shndx];
    if (sec->out_off == INVALIDO || s->value > sec->size) return 0;
    *offset = sec->out_off + s->value;
    return 1;
}

static int gravar_elf(const char *path, unsigned entrada)
{
    unsigned total = PAGINA_ELF + tamanho_imagem;
    unsigned char *out = (unsigned char *)calloc(1, total);
    if (!out) { falhar(path, "memoria insuficiente para ELF executavel"); return 0; }
    memcpy(out, "\177ELF", 4); out[4] = 1; out[5] = 1; out[6] = 1;
    grava16(out + 16, 2); grava16(out + 18, EM_386); grava32(out + 20, 1);
    grava32(out + 24, BASE_ELF + PAGINA_ELF + entrada);
    grava32(out + 28, 52); grava32(out + 32, 0); grava32(out + 36, 0);
    grava16(out + 40, 52); grava16(out + 42, 32); grava16(out + 44, 1);
    grava16(out + 46, 40); grava16(out + 48, 0); grava16(out + 50, 0);
    grava32(out + 52, 1); grava32(out + 56, PAGINA_ELF);
    grava32(out + 60, BASE_ELF + PAGINA_ELF); grava32(out + 64, BASE_ELF + PAGINA_ELF);
    grava32(out + 68, tamanho_imagem); grava32(out + 72, tamanho_imagem);
    grava32(out + 76, 7); grava32(out + 80, PAGINA_ELF);
    memcpy(out + PAGINA_ELF, imagem, tamanho_imagem);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) { free(out); fprintf(stderr, "ldagui: %s: %s\n", path, strerror(errno)); return 0; }
    int ok = escrever_tudo(fd, out, total) >= 0;
    if (close(fd) < 0) ok = 0;
    free(out);
    if (!ok) { (void)unlink(path); fprintf(stderr, "ldagui: erro ao gravar %s\n", path); return 0; }
    return 1;
}

int principal(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "version")) {
        puts("ldagui 0.1: ligador estatico ELF32 i386 (R_386_32/PC32/PLT32)");
        return 0;
    }
    if (argc < 5 || strcmp(argv[1], "link")) {
        fprintf(stderr, "uso: ldagui link -o saida.elf [--entry simbolo] objeto.o...\n");
        return 1;
    }
    const char *saida = NULL, *entrada_nome = "_start";
    const char *arquivos[OBJETOS_MAX]; unsigned n_arquivos = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) saida = argv[++i];
        else if (!strcmp(argv[i], "--entry") && i + 1 < argc) entrada_nome = argv[++i];
        else if (argv[i][0] == '-') { fprintf(stderr, "ldagui: opcao desconhecida: %s\n", argv[i]); return 1; }
        else if (n_arquivos < OBJETOS_MAX) arquivos[n_arquivos++] = argv[i];
        else { fprintf(stderr, "ldagui: limite de %u objetos\n", OBJETOS_MAX); return 1; }
    }
    if (!saida || !n_arquivos) { fprintf(stderr, "ldagui: informe -o e pelo menos um objeto\n"); return 1; }
    for (unsigned i = 0; i < n_arquivos; i++) if (!strcmp(saida, arquivos[i])) {
        fprintf(stderr, "ldagui: saida coincide com um objeto de entrada\n"); return 1;
    }
    n_objetos = n_arquivos;
    for (unsigned i = 0; i < n_objetos; i++) if (!carregar_objeto(&objetos[i], arquivos[i])) return 1;
    if (!preparar_globais() || !organizar_secoes()) return 1;
    unsigned entrada;
    if (!procurar_entrada(entrada_nome, &entrada)) {
        fprintf(stderr, "ldagui: simbolo de entrada nao definido: %s\n", entrada_nome); return 1;
    }
    if (!aplicar_relocations() || erro) return 1;
    if (!gravar_elf(saida, entrada)) return 1;
    printf("ldagui: %s linkado (%u objetos, %u bytes de imagem, entry=%s)\n",
           saida, n_objetos, tamanho_imagem, entrada_nome);
    return 0;
}
