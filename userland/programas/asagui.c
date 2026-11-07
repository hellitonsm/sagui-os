/* asagui - assembler i386 freestanding para o SaguiOS.
 *
 * Primeira versao didatica: duas passagens, sintaxe inspirada em NASM e ELF32
 * estatico com um PT_LOAD. Nao e NASM nem aceita codigo objeto/linker script. */
#include "sagui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define FONTE_MAX   16384u
#define CODIGO_MAX  32768u
#define LINHAS_MAX  4096
#define SIMBOLOS_MAX 512
#define NOME_MAXIMO 64
#define PAGINA_ELF  4096u
#define BASE_ELF    0x40000000u

typedef struct {
    unsigned inicio, tamanho, numero, endereco, bytes, instrucao;
} linha_t;
typedef struct {
    char nome[NOME_MAXIMO];
    unsigned endereco;
    unsigned definido;
    unsigned global;
    unsigned externo;
} simbolo_t;
typedef struct {
    unsigned offset;
    unsigned simbolo;
    unsigned tipo;
} reloc_t;

#define RELOC_MAX 2048u
#define OBJETO_MAX 131072u

enum { R_386_32 = 1, R_386_PC32 = 2, R_386_PLT32 = 4 };

static char fonte[FONTE_MAX + 1];
static unsigned char codigo[CODIGO_MAX];
static unsigned char imagem[OBJETO_MAX];
static reloc_t relocacoes[RELOC_MAX];
static unsigned n_relocacoes;
static int modo_objeto;
static linha_t linhas[LINHAS_MAX];
static simbolo_t simbolos[SIMBOLOS_MAX];
static unsigned n_linhas, n_simbolos, tamanho_codigo, entrada;
static int erro;

static void falhar(unsigned linha, const char *mensagem)
{
    if (!erro) fprintf(stderr, "asagui:%u: %s\n", linha, mensagem);
    erro = 1;
}

static int espaco(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char *aparar(char *s)
{
    while (*s && espaco((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n && espaco((unsigned char)s[n - 1])) s[--n] = 0;
    return s;
}
static char *aparar_no_lugar(char *s)
{
    char *p = aparar(s);
    if (p != s) memmove(s, p, strlen(p) + 1);
    return s;
}

static int igual_sem_caso(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z') x = (char)(x + ('a' - 'A'));
        if (y >= 'A' && y <= 'Z') y = (char)(y + ('a' - 'A'));
        if (x != y) return 0;
    }
    return *a == 0 && *b == 0;
}

static int nome_inicio(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.' || c == '$';
}
static int nome_parte(int c)
{
    return nome_inicio(c) || (c >= '0' && c <= '9');
}

/* Rotulos desta sintaxe ocupam o primeiro token: rotulo: instrucao. */
static char *achar_dois_pontos(char *s)
{
    for (; *s && !espaco((unsigned char)*s); s++) {
        if (*s == ':') return s;
    }
    return NULL;
}

/* Remove comentarios, mantendo delimitadores dentro de uma string. */
static void linha_texto(const linha_t *l, char saida[256])
{
    unsigned n = l->tamanho;
    if (n > 255) n = 255;
    memcpy(saida, fonte + l->inicio, n);
    saida[n] = 0;
    int aspas = 0, escape = 0;
    for (unsigned i = 0; i < n; i++) {
        char c = saida[i];
        if (aspas) {
            if (escape) escape = 0;
            else if (c == '\\') escape = 1;
            else if (c == '"') aspas = 0;
        } else if (c == '"') aspas = 1;
        else if (c == ';' || c == '#') { saida[i] = 0; break; }
    }
}

static int simbolo_buscar(const char *nome)
{
    for (unsigned i = 0; i < n_simbolos; i++)
        if (igual_sem_caso(simbolos[i].nome, nome)) return (int)i;
    return -1;
}

static int simbolo_nome_valido(const char *nome, unsigned linha)
{
    if (!nome[0] || !nome_inicio((unsigned char)nome[0])) {
        falhar(linha, "nome de simbolo invalido");
        return 0;
    }
    for (const char *p = nome + 1; *p; p++) {
        if (!nome_parte((unsigned char)*p)) {
            falhar(linha, "caractere invalido em simbolo");
            return 0;
        }
    }
    if (strlen(nome) >= NOME_MAXIMO) {
        falhar(linha, "simbolo longo demais");
        return 0;
    }
    return 1;
}

static int simbolo_garantir(const char *nome, unsigned linha)
{
    if (!simbolo_nome_valido(nome, linha)) return -1;
    int i = simbolo_buscar(nome);
    if (i >= 0) return i;
    if (n_simbolos == SIMBOLOS_MAX) {
        falhar(linha, "limite de simbolos atingido");
        return -1;
    }
    i = (int)n_simbolos++;
    memset(&simbolos[i], 0, sizeof simbolos[i]);
    copia_texto(simbolos[i].nome, nome, sizeof simbolos[i].nome);
    return i;
}

static int simbolo_adicionar(const char *nome, unsigned endereco, unsigned linha)
{
    int i = simbolo_garantir(nome, linha);
    if (i < 0) return 0;
    if (simbolos[i].definido || simbolos[i].externo) {
        falhar(linha, "rotulo duplicado ou declarado extern");
        return 0;
    }
    simbolos[i].endereco = endereco;
    simbolos[i].definido = 1;
    if (igual_sem_caso(nome, "_start") || igual_sem_caso(nome, "start")) entrada = endereco;
    return 1;
}

static int numero(const char *texto, unsigned *valor)
{
    const char *p = texto;
    if (*p == '+' || *p == '-') p++;
    int base = (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) ? 16 : 10;
    char *fim;
    errno = 0;
    long long v = strtoll(texto, &fim, base);
    if (fim == texto || *aparar(fim) || errno == ERANGE) return 0;
    if (v < (-2147483647LL - 1) || v > 0xFFFFFFFFLL) return 0;
    *valor = (unsigned)v;
    return 1;
}

static int valor_ou_rotulo(const char *texto, unsigned *valor)
{
    if (numero(texto, valor)) return 1;
    int i = simbolo_buscar(texto);
    if (i < 0) return 0;
    *valor = BASE_ELF + PAGINA_ELF + simbolos[i].endereco;
    return 1;
}

static const char *argumentos_diretos(const char *texto)
{
    while (*texto && espaco((unsigned char)*texto)) texto++;
    while (*texto && !espaco((unsigned char)*texto) && *texto != ',') texto++;
    while (*texto && espaco((unsigned char)*texto)) texto++;
    return texto;
}

static void separar(const char *texto, char *mnemonico, size_t mmax,
                    char op1[96], char op2[96], int *qtd)
{
    while (*texto && espaco((unsigned char)*texto)) texto++;
    size_t m = 0;
    while (*texto && !espaco((unsigned char)*texto) && *texto != ',') {
        if (m + 1 < mmax) mnemonico[m++] = *texto;
        texto++;
    }
    mnemonico[m] = 0;
    while (*texto && espaco((unsigned char)*texto)) texto++;
    *qtd = 0;
    if (!*texto) return;
    int colchetes = 0, aspas = 0, escape = 0;
    char *dest = op1;
    size_t usados = 0;
    for (; *texto; texto++) {
        char c = *texto;
        if (aspas) {
            if (usados + 1 < 96) dest[usados++] = c;
            if (escape) escape = 0;
            else if (c == '\\') escape = 1;
            else if (c == '"') aspas = 0;
            continue;
        }
        if (c == '"') { aspas = 1; if (usados + 1 < 96) dest[usados++] = c; continue; }
        if (c == '[') colchetes++;
        if (c == ']') colchetes--;
        if (c == ',' && !colchetes) {
            dest[usados] = 0;
            if (dest == op1) { *qtd = 1; dest = op2; usados = 0; continue; }
            *qtd = 3; return;
        }
        if (usados + 1 < 96) dest[usados++] = c;
    }
    dest[usados] = 0;
    if (dest == op1) *qtd = 1;
    else *qtd = 2;
    aparar_no_lugar(op1);
    aparar_no_lugar(op2);
}

static int registrador(const char *nome)
{
    static const char *const nomes[] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    for (int i = 0; i < 8; i++) if (igual_sem_caso(nome, nomes[i])) return i;
    return -1;
}

static int memoria_rotulo(const char *operando, char *nome)
{
    size_t n = strlen(operando);
    if (n < 3 || operando[0] != '[' || operando[n - 1] != ']') return 0;
    memcpy(nome, operando + 1, n - 2);
    nome[n - 2] = 0;
    aparar_no_lugar(nome);
    return nome[0] != 0;
}

/* Enderecamento reduzido [reg], [reg+desloc] e [reg-desloc], disp8. */
static int memoria_base(const char *operando, int *base, int *desloc)
{
    size_t n = strlen(operando);
    if (n < 3 || operando[0] != '[' || operando[n - 1] != ']') return 0;
    char texto[96];
    if (n - 2 >= sizeof texto) return 0;
    memcpy(texto, operando + 1, n - 2); texto[n - 2] = 0;
    char compacto[96]; unsigned k = 0;
    for (unsigned i = 0; texto[i]; i++) if (!espaco((unsigned char)texto[i])) compacto[k++] = texto[i];
    compacto[k] = 0;
    char reg[8]; unsigned r = 0;
    while (compacto[r] && compacto[r] != '+' && compacto[r] != '-') {
        if (r + 1 >= sizeof reg) return 0;
        reg[r] = compacto[r]; r++;
    }
    reg[r] = 0;
    int b = registrador(reg);
    if (b < 0) return 0;
    *desloc = 0;
    if (compacto[r]) {
        char *fim;
        errno = 0;
        long long v = strtoll(compacto + r, &fim, 0);
        if (fim == compacto + r || *fim || errno == ERANGE || v < -128 || v > 127) return 0;
        *desloc = (int)v;
    }
    *base = b;
    return 1;
}

static int decodificar_string(const char *s, unsigned char *destino, unsigned capacidade, unsigned *n)
{
    *n = 0;
    if (*s++ != '"') return 0;
    while (*s && *s != '"') {
        unsigned char c = (unsigned char)*s++;
        if (c == '\\') {
            if (!*s) return 0;
            c = (unsigned char)*s++;
            if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            else if (c != '\\' && c != '"' && c != '\'') return 0;
        }
        if (*n == capacidade) return 0;
        destino[(*n)++] = c;
    }
    if (*s != '"') return 0;
    s++;
    return !*aparar((char *)s);
}

static int dados_tamanho(const char *args, unsigned largura, unsigned linha, unsigned *tamanho)
{
    unsigned total = 0;
    char item[96];
    while (*args) {
        while (*args && (espaco((unsigned char)*args) || *args == ',')) args++;
        if (!*args) break;
        unsigned n = 0;
        if (*args == '"') {
            int aspas = 1, escape = 0;
            const char *inicio = args++;
            while (*args && aspas) {
                char c = *args++;
                if (escape) escape = 0;
                else if (c == '\\') escape = 1;
                else if (c == '"') aspas = 0;
            }
            if (aspas) { falhar(linha, "string db nao terminada"); return 0; }
            n = (unsigned)(args - inicio);
            unsigned char temp[256]; unsigned bytes;
            char str[256];
            if (n >= sizeof str) { falhar(linha, "string db longa demais"); return 0; }
            memcpy(str, inicio, n); str[n] = 0;
            if (!decodificar_string(str, temp, sizeof temp, &bytes)) { falhar(linha, "escape db invalido"); return 0; }
            n = bytes;
        } else {
            const char *inicio = args;
            while (*args && *args != ',') args++;
            n = (unsigned)(args - inicio);
            if (n >= sizeof item) { falhar(linha, "valor db/dd longo demais"); return 0; }
            memcpy(item, inicio, n); item[n] = 0;
            unsigned v;
            char *token = aparar(item);
            if (!numero(token, &v)) {
                if (largura == 1 || !nome_inicio((unsigned char)token[0])) {
                    falhar(linha, "valor numerico/rotulo invalido"); return 0;
                }
                for (char *p = token + 1; *p; p++) {
                    if (!nome_parte((unsigned char)*p)) { falhar(linha, "rotulo invalido em dd"); return 0; }
                }
            } else if (largura == 1 && (v > 255u && v < 0xFFFFFF80u)) {
                falhar(linha, "valor db fora de -128..255"); return 0;
            }
            n = largura;
        }
        if (total > CODIGO_MAX - n) { falhar(linha, "dados excedem limite de codigo"); return 0; }
        total += n;
        while (*args && espaco((unsigned char)*args)) args++;
        if (*args == ',') args++;
        else if (*args) { falhar(linha, "esperado virgula em diretiva de dados"); return 0; }
    }
    *tamanho = total;
    return 1;
}

static int tamanho_instrucao(const char *texto, unsigned linha, unsigned *tamanho)
{
    char mn[24] = {0}, a[96] = {0}, b[96] = {0}; int n;
    separar(texto, mn, sizeof mn, a, b, &n);
    if (!mn[0]) { *tamanho = 0; return 1; }
    if (igual_sem_caso(mn, "bits")) {
        unsigned v;
        if (n != 1 || !numero(a, &v) || v != 32) { falhar(linha, "somente bits 32 e aceito"); return 0; }
        *tamanho = 0; return 1;
    }
    if (igual_sem_caso(mn, "section")) {
        if (n != 1 || (!igual_sem_caso(a, ".text") && !igual_sem_caso(a, ".rodata") && !igual_sem_caso(a, ".data"))) {
            falhar(linha, "section aceita somente .text, .rodata ou .data (sem separacao fisica)"); return 0;
        }
        *tamanho = 0; return 1;
    }
    if (igual_sem_caso(mn, "global")) {
        if (n != 1 || !simbolo_nome_valido(a, linha)) { falhar(linha, "global exige um nome de simbolo"); return 0; }
        if (modo_objeto) {
            int i = simbolo_garantir(a, linha);
            if (i < 0) return 0;
            simbolos[i].global = 1;
        }
        *tamanho = 0; return 1;
    }
    if (igual_sem_caso(mn, "extern")) {
        if (!modo_objeto) { falhar(linha, "extern so e aceito ao montar um objeto relocavel"); return 0; }
        if (n != 1 || !simbolo_nome_valido(a, linha)) { falhar(linha, "extern exige um nome de simbolo"); return 0; }
        int i = simbolo_garantir(a, linha);
        if (i < 0 || simbolos[i].definido) { falhar(linha, "simbolo extern ja definido localmente"); return 0; }
        simbolos[i].global = 1;
        simbolos[i].externo = 1;
        *tamanho = 0; return 1;
    }
    if (igual_sem_caso(mn, "align")) { falhar(linha, "align ainda nao e suportado"); return 0; }
    if (igual_sem_caso(mn, "db") || igual_sem_caso(mn, "dd"))
        return dados_tamanho(argumentos_diretos(texto), igual_sem_caso(mn, "db") ? 1u : 4u, linha, tamanho);
    if (igual_sem_caso(mn, "ret") || igual_sem_caso(mn, "leave") ||
        igual_sem_caso(mn, "nop") || igual_sem_caso(mn, "hlt") ||
        igual_sem_caso(mn, "cdq")) {
        if (n) { falhar(linha, "instrucao nao recebe operandos"); return 0; }
        *tamanho = 1; return 1;
    }
    if (igual_sem_caso(mn, "int")) { if (n != 1) goto operands; *tamanho = 2; return 1; }
    if (igual_sem_caso(mn, "push") || igual_sem_caso(mn, "pop") ||
        igual_sem_caso(mn, "inc") || igual_sem_caso(mn, "dec") ||
        igual_sem_caso(mn, "idiv") || igual_sem_caso(mn, "neg")) {
        if (n != 1) goto operands;
        if (igual_sem_caso(mn, "push")) *tamanho = registrador(a) >= 0 ? 1u : 5u;
        else if (igual_sem_caso(mn, "pop")) { if (registrador(a) < 0) goto bad_operand; *tamanho = 1; }
        else if (igual_sem_caso(mn, "inc") || igual_sem_caso(mn, "dec")) { if (registrador(a) < 0) goto bad_operand; *tamanho = 1; }
        else { if (registrador(a) < 0) goto bad_operand; *tamanho = 2; }
        return 1;
    }
    if (igual_sem_caso(mn, "jmp") || igual_sem_caso(mn, "call")) {
        if (n != 1) goto operands;
        *tamanho = 5;
        return 1;
    }
    if (igual_sem_caso(mn, "je") || igual_sem_caso(mn, "jz") ||
        igual_sem_caso(mn, "jne") || igual_sem_caso(mn, "jnz") ||
        igual_sem_caso(mn, "jl") || igual_sem_caso(mn, "jle") ||
        igual_sem_caso(mn, "jg") || igual_sem_caso(mn, "jge")) {
        if (n != 1) goto operands;
        *tamanho = 6;
        return 1;
    }
    if (igual_sem_caso(mn, "mov") || igual_sem_caso(mn, "lea")) {
        if (n != 2) goto operands;
        char mem[96]; int base, desloc;
        int d = registrador(a), s = registrador(b);
        if (igual_sem_caso(mn, "lea")) {
            if (d < 0) goto bad_operand;
            if (memoria_base(b, &base, &desloc)) *tamanho = 3u + (base == 4 ? 1u : 0u);
            else if (memoria_rotulo(b, mem)) *tamanho = 6;
            else goto bad_operand;
            return 1;
        }
        if (d >= 0) {
            if (s >= 0) *tamanho = 2;
            else if (memoria_base(b, &base, &desloc)) *tamanho = 3u + (base == 4 ? 1u : 0u);
            else if (memoria_rotulo(b, mem)) *tamanho = 6;
            else *tamanho = 5;
        } else if (s >= 0 && memoria_base(a, &base, &desloc)) *tamanho = 3u + (base == 4 ? 1u : 0u);
        else if (s >= 0 && memoria_rotulo(a, mem)) *tamanho = 6;
        else goto bad_operand;
        return 1;
    }
    if (n == 2 && (igual_sem_caso(mn, "add") || igual_sem_caso(mn, "sub") ||
                   igual_sem_caso(mn, "cmp") || igual_sem_caso(mn, "and") ||
                   igual_sem_caso(mn, "or") || igual_sem_caso(mn, "xor") ||
                   igual_sem_caso(mn, "test") || igual_sem_caso(mn, "imul"))) {
        int d = registrador(a), s = registrador(b);
        if (d < 0) goto bad_operand;
        if (s >= 0) *tamanho = igual_sem_caso(mn, "imul") ? 3u : 2u;
        else if (igual_sem_caso(mn, "test") || igual_sem_caso(mn, "imul")) goto bad_operand;
        else *tamanho = 6;
        return 1;
    }
    falhar(linha, "instrucao/operando nao suportado");
    return 0;
operands:
    falhar(linha, "quantidade de operandos invalida"); return 0;
bad_operand:
    falhar(linha, "registrador ou operando nao suportado"); return 0;
}

static int pass1(void)
{
    unsigned pos = 0, num = 1, offset = 0;
    unsigned tamanho_fonte = (unsigned)strlen(fonte);
    while (pos < tamanho_fonte) {
        if (n_linhas == LINHAS_MAX) { falhar(num, "limite de linhas atingido"); return 0; }
        unsigned ini = pos;
        while (fonte[pos] && fonte[pos] != '\n') pos++;
        unsigned len = pos - ini;
        if (fonte[pos] == '\n') pos++;
        if (len > 255) { falhar(num, "linha excede 255 bytes"); return 0; }
        linha_t *l = &linhas[n_linhas++];
        l->inicio = ini; l->tamanho = len; l->numero = num; l->endereco = offset; l->bytes = 0; l->instrucao = 0;
        char linha_buf[256]; linha_texto(l, linha_buf);
        char *s = aparar(linha_buf);
        if (!*s) { num++; continue; }
        char *colon = achar_dois_pontos(s);
        if (colon) {
            int valido = 1;
            for (char *p = s; p < colon; p++) if (!nome_parte((unsigned char)*p)) valido = 0;
            *colon = 0;
            char *label = aparar(s);
            if (!valido || !simbolo_adicionar(label, offset, num)) return 0;
            s = aparar(colon + 1);
            l->instrucao = (unsigned)(s - linha_buf);
            if (!*s) { num++; continue; }
        } else {
            l->instrucao = (unsigned)(s - linha_buf);
        }
        unsigned bytes = 0;
        if (!tamanho_instrucao(s, num, &bytes)) return 0;
        l->bytes = bytes;
        if (offset > CODIGO_MAX - bytes) { falhar(num, "codigo excede 32 KiB"); return 0; }
        offset += bytes;
        num++;
    }
    tamanho_codigo = offset;
    if (modo_objeto) return 1;
    int start = simbolo_buscar("_start");
    if (start < 0) start = simbolo_buscar("start");
    if (start < 0 || !simbolos[start].definido) { falhar(1, "rotulo _start: ou start: ausente"); return 0; }
    entrada = simbolos[start].endereco;
    if (entrada >= tamanho_codigo) { falhar(1, "rotulo de entrada nao aponta para codigo/dados carregados"); return 0; }
    return 1;
}

static void emitir(unsigned char **p, unsigned v) { *(*p)++ = (unsigned char)v; }
static void emitir32(unsigned char **p, unsigned v)
{
    emitir(p, v); emitir(p, v >> 8); emitir(p, v >> 16); emitir(p, v >> 24);
}
static int imediato_ou_rotulo(const char *s, unsigned *v)
{
    int i = simbolo_buscar(s);
    if (i >= 0 && !simbolos[i].definido) return 0;
    return valor_ou_rotulo(s, v);
}

static int reloc_adicionar(unsigned offset, const char *nome, unsigned tipo, unsigned linha)
{
    if (!modo_objeto) { falhar(linha, "referencia nao resolvida"); return 0; }
    int si = simbolo_garantir(nome, linha);
    if (si < 0) return 0;
    if (!simbolos[si].definido) {
        simbolos[si].global = 1;
        simbolos[si].externo = 1;
    }
    if (n_relocacoes == RELOC_MAX) { falhar(linha, "limite de relocations atingido"); return 0; }
    relocacoes[n_relocacoes].offset = offset;
    relocacoes[n_relocacoes].simbolo = (unsigned)si;
    relocacoes[n_relocacoes].tipo = tipo;
    n_relocacoes++;
    return 1;
}

static int emitir_rel32(unsigned char **p, const char *nome, unsigned tipo,
                        int addend, unsigned linha)
{
    unsigned offset = (unsigned)(*p - codigo);
    if (!reloc_adicionar(offset, nome, tipo, linha)) return 0;
    emitir32(p, (unsigned)addend);
    return 1;
}

static int emitir_valor32(unsigned char **p, const char *texto, unsigned linha)
{
    unsigned valor;
    if (numero(texto, &valor)) { emitir32(p, valor); return 1; }
    if (modo_objeto) return emitir_rel32(p, texto, R_386_32, 0, linha);
    if (imediato_ou_rotulo(texto, &valor)) { emitir32(p, valor); return 1; }
    falhar(linha, "imediato ou rotulo nao definido");
    return 0;
}

static int alvo_relativo(const char *s, unsigned offset, unsigned tamanho, unsigned *v, unsigned linha)
{
    int i = simbolo_buscar(s);
    if (modo_objeto) return i >= 0 ? 1 : simbolo_garantir(s, linha) >= 0;
    if (i < 0 || !simbolos[i].definido) { falhar(linha, "rotulo de salto nao definido"); return 0; }
    *v = simbolos[i].endereco - (offset + tamanho);
    return 1;
}

static int emitir_dados(char *args, unsigned largura, unsigned char **p, unsigned linha)
{
    while (*args) {
        while (*args && (espaco((unsigned char)*args) || *args == ',')) args++;
        if (!*args) break;
        if (*args == '"') {
            const char *inicio = args, *q = args + 1;
            int escape = 0, fechou = 0;
            while (*q) {
                char c = *q++;
                if (escape) escape = 0;
                else if (c == '\\') escape = 1;
                else if (c == '"') { fechou = 1; break; }
            }
            unsigned tamanho = (unsigned)(q - inicio);
            char texto_string[256]; unsigned char temp[256]; unsigned n;
            if (!fechou || tamanho >= sizeof texto_string) { falhar(linha, "string db invalida/longa"); return 0; }
            memcpy(texto_string, inicio, tamanho); texto_string[tamanho] = 0;
            if (!decodificar_string(texto_string, temp, sizeof temp, &n)) { falhar(linha, "escape db invalido"); return 0; }
            for (unsigned i = 0; i < n; i++) emitir(p, temp[i]);
            args = (char *)q;
        } else {
            char item[96]; unsigned n = 0;
            while (*args && *args != ',') { if (n + 1 < sizeof item) item[n++] = *args; args++; }
            item[n] = 0;
            unsigned v;
            char *valor_texto = aparar(item);
            if (largura == 1) {
                if (!numero(valor_texto, &v)) { falhar(linha, "db aceita somente valores numericos ou strings"); return 0; }
                if (v > 255u && v < 0xFFFFFF80u) { falhar(linha, "valor db fora de faixa"); return 0; }
                emitir(p, v);
            } else if (numero(valor_texto, &v)) {
                emitir32(p, v);
            } else if (modo_objeto) {
                if (!emitir_rel32(p, valor_texto, R_386_32, 0, linha)) return 0;
            } else if (imediato_ou_rotulo(valor_texto, &v)) {
                emitir32(p, v);
            } else { falhar(linha, "valor dd/rotulo nao definido"); return 0; }
        }
        while (*args && espaco((unsigned char)*args)) args++;
        if (*args == ',') args++;
        else if (*args) { falhar(linha, "esperado virgula em diretiva de dados"); return 0; }
    }
    return 1;
}

static int codificar(const linha_t *l, const char *texto, unsigned char **destino)
{
    char mn[24] = {0}, a[96] = {0}, b[96] = {0}; int n;
    separar(texto, mn, sizeof mn, a, b, &n);
    unsigned char *p = *destino;
    unsigned v;
    if (!mn[0] || igual_sem_caso(mn, "bits") || igual_sem_caso(mn, "section") ||
        igual_sem_caso(mn, "global") || igual_sem_caso(mn, "extern") ||
        igual_sem_caso(mn, "align")) return 1;
    if (igual_sem_caso(mn, "db") || igual_sem_caso(mn, "dd")) {
        if (!emitir_dados((char *)argumentos_diretos(texto), igual_sem_caso(mn, "db") ? 1u : 4u, &p, l->numero)) return 0;
        *destino = p; return 1;
    }
    if (igual_sem_caso(mn, "ret")) emitir(&p, 0xC3);
    else if (igual_sem_caso(mn, "leave")) emitir(&p, 0xC9);
    else if (igual_sem_caso(mn, "nop")) emitir(&p, 0x90);
    else if (igual_sem_caso(mn, "hlt")) emitir(&p, 0xF4);
    else if (igual_sem_caso(mn, "cdq")) emitir(&p, 0x99);
    else if (igual_sem_caso(mn, "int")) {
        if (!numero(a, &v) || v > 255) { falhar(l->numero, "vetor int invalido"); return 0; }
        emitir(&p, 0xCD); emitir(&p, v);
    } else if (igual_sem_caso(mn, "push")) {
        int r = registrador(a);
        if (r >= 0) emitir(&p, 0x50 + r);
        else {
            emitir(&p, 0x68);
            if (!emitir_valor32(&p, a, l->numero)) return 0;
        }
    } else if (igual_sem_caso(mn, "pop") || igual_sem_caso(mn, "inc") || igual_sem_caso(mn, "dec")) {
        int r = registrador(a); if (r < 0) { falhar(l->numero, "registrador invalido"); return 0; }
        emitir(&p, (igual_sem_caso(mn, "pop") ? 0x58 : igual_sem_caso(mn, "inc") ? 0x40 : 0x48) + r);
    } else if (igual_sem_caso(mn, "idiv") || igual_sem_caso(mn, "neg")) {
        int r = registrador(a); if (r < 0) { falhar(l->numero, "registrador invalido"); return 0; }
        emitir(&p, 0xF7); emitir(&p, (igual_sem_caso(mn, "idiv") ? 0xF8 : 0xD8) + r);
    } else if (igual_sem_caso(mn, "jmp") || igual_sem_caso(mn, "call")) {
        emitir(&p, igual_sem_caso(mn, "jmp") ? 0xE9 : 0xE8);
        if (modo_objeto) {
            if (!emitir_rel32(&p, a, R_386_PC32, -4, l->numero)) return 0;
        } else {
            if (!alvo_relativo(a, l->endereco, 5, &v, l->numero)) return 0;
            emitir32(&p, v);
        }
    } else if (igual_sem_caso(mn, "je") || igual_sem_caso(mn, "jz") ||
               igual_sem_caso(mn, "jne") || igual_sem_caso(mn, "jnz") ||
               igual_sem_caso(mn, "jl") || igual_sem_caso(mn, "jle") ||
               igual_sem_caso(mn, "jg") || igual_sem_caso(mn, "jge")) {
        unsigned cond = igual_sem_caso(mn, "je") || igual_sem_caso(mn, "jz") ? 0x84 :
                        igual_sem_caso(mn, "jne") || igual_sem_caso(mn, "jnz") ? 0x85 :
                        igual_sem_caso(mn, "jl") ? 0x8C : igual_sem_caso(mn, "jle") ? 0x8E :
                        igual_sem_caso(mn, "jg") ? 0x8F : 0x8D;
        emitir(&p, 0x0F); emitir(&p, cond);
        if (modo_objeto) {
            if (!emitir_rel32(&p, a, R_386_PC32, -4, l->numero)) return 0;
        } else {
            if (!alvo_relativo(a, l->endereco, 6, &v, l->numero)) return 0;
            emitir32(&p, v);
        }
    } else if (igual_sem_caso(mn, "mov") || igual_sem_caso(mn, "lea")) {
        int d = registrador(a), s = registrador(b), base, desloc; char sym[96];
        if (d >= 0 && s >= 0 && igual_sem_caso(mn, "mov")) {
            emitir(&p, 0x89); emitir(&p, 0xC0 | (s << 3) | d);
        } else if (d >= 0 && memoria_base(b, &base, &desloc)) {
            emitir(&p, igual_sem_caso(mn, "lea") ? 0x8D : 0x8B);
            emitir(&p, 0x40 | (d << 3) | base);
            if (base == 4) emitir(&p, 0x24);
            emitir(&p, (unsigned char)desloc);
        } else if (d >= 0 && memoria_rotulo(b, sym)) {
            emitir(&p, igual_sem_caso(mn, "lea") ? 0x8D : 0x8B);
            emitir(&p, 0x05 | (d << 3));
            if (!emitir_valor32(&p, sym, l->numero)) return 0;
        } else if (d >= 0 && igual_sem_caso(mn, "mov")) {
            emitir(&p, 0xB8 + d);
            if (!emitir_valor32(&p, b, l->numero)) return 0;
        } else if (s >= 0 && memoria_base(a, &base, &desloc) && igual_sem_caso(mn, "mov")) {
            emitir(&p, 0x89); emitir(&p, 0x40 | (s << 3) | base);
            if (base == 4) emitir(&p, 0x24);
            emitir(&p, (unsigned char)desloc);
        } else if (memoria_rotulo(a, sym) && s >= 0 && igual_sem_caso(mn, "mov")) {
            emitir(&p, 0x89); emitir(&p, 0x05 | (s << 3));
            if (!emitir_valor32(&p, sym, l->numero)) return 0;
        } else { falhar(l->numero, "mov/lea nao suportado"); return 0; }
    } else if (n == 2 && (igual_sem_caso(mn, "add") || igual_sem_caso(mn, "sub") ||
                          igual_sem_caso(mn, "cmp") || igual_sem_caso(mn, "and") ||
                          igual_sem_caso(mn, "or") || igual_sem_caso(mn, "xor") ||
                          igual_sem_caso(mn, "test") || igual_sem_caso(mn, "imul"))) {
        int d = registrador(a), s = registrador(b);
        if (d < 0) { falhar(l->numero, "registrador de destino invalido"); return 0; }
        if (s >= 0) {
            if (igual_sem_caso(mn, "imul")) { emitir(&p, 0x0F); emitir(&p, 0xAF); emitir(&p, 0xC0 | (d << 3) | s); }
            else {
                unsigned op = igual_sem_caso(mn, "add") ? 0x01 : igual_sem_caso(mn, "sub") ? 0x29 :
                              igual_sem_caso(mn, "cmp") ? 0x39 : igual_sem_caso(mn, "and") ? 0x21 :
                              igual_sem_caso(mn, "or") ? 0x09 : igual_sem_caso(mn, "xor") ? 0x31 : 0x85;
                emitir(&p, op); emitir(&p, 0xC0 | (s << 3) | d);
            }
        } else {
            unsigned ext = igual_sem_caso(mn, "add") ? 0 : igual_sem_caso(mn, "or") ? 1 :
                           igual_sem_caso(mn, "and") ? 4 : igual_sem_caso(mn, "sub") ? 5 :
                           igual_sem_caso(mn, "xor") ? 6 : 7;
            emitir(&p, 0x81); emitir(&p, 0xC0 | (ext << 3) | d);
            if (!emitir_valor32(&p, b, l->numero)) return 0;
        }
    } else {
        falhar(l->numero, "instrucao nao suportada"); return 0;
    }
    *destino = p;
    return 1;
}

static int pass2(void)
{
    unsigned char *out = codigo;
    for (unsigned i = 0; i < n_linhas && !erro; i++) {
        linha_t *l = &linhas[i];
        if (!l->bytes) continue;
        char buf[256]; linha_texto(l, buf);
        char *s = aparar(buf);
        char *colon = achar_dois_pontos(s);
        if (colon) s = aparar(colon + 1);
        if (!*s) continue;
        unsigned char *antes = out;
        if (!codificar(l, s, &out)) return 0;
        if ((unsigned)(out - antes) != l->bytes) { falhar(l->numero, "erro interno de tamanho de instrucao"); return 0; }
    }
    return !erro;
}

static void gravar32(unsigned offset, unsigned valor, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; i++) imagem[offset + i] = (unsigned char)(valor >> (8 * i));
}

static unsigned criar_elf(void)
{
    unsigned total = PAGINA_ELF + tamanho_codigo;
    memset(imagem, 0, total);
    imagem[0] = 0x7F; imagem[1] = 'E'; imagem[2] = 'L'; imagem[3] = 'F';
    imagem[4] = 1; imagem[5] = 1; imagem[6] = 1;
    gravar32(16, 2, 2); gravar32(18, 3, 2); gravar32(20, 1, 4);
    gravar32(24, BASE_ELF + PAGINA_ELF + entrada, 4);
    gravar32(28, 52, 4); gravar32(40, 52, 2); gravar32(42, 32, 2); gravar32(44, 1, 2);
    gravar32(52, 1, 4); gravar32(56, PAGINA_ELF, 4);
    gravar32(60, BASE_ELF + PAGINA_ELF, 4); gravar32(64, BASE_ELF + PAGINA_ELF, 4);
    gravar32(68, tamanho_codigo, 4); gravar32(72, tamanho_codigo, 4);
    gravar32(76, 7, 4); gravar32(80, PAGINA_ELF, 4);
    memcpy(imagem + PAGINA_ELF, codigo, tamanho_codigo);
    return total;
}

static unsigned alinhar(unsigned n, unsigned a)
{
    return (n + a - 1u) & ~(a - 1u);
}

static unsigned adicionar_nome(char *tabela, unsigned capacidade, unsigned *usado, const char *nome)
{
    unsigned n = (unsigned)strlen(nome) + 1u;
    if (*usado > capacidade - n) return 0xFFFFFFFFu;
    unsigned off = *usado;
    memcpy(tabela + off, nome, n);
    *usado += n;
    return off;
}

static unsigned criar_objeto(void)
{
    static char strtab[OBJETO_MAX / 2];
    static char shstrtab[128];
    static unsigned nome_off[SIMBOLOS_MAX];
    static unsigned ordem[SIMBOLOS_MAX];
    static unsigned indice_obj[SIMBOLOS_MAX];
    unsigned str_used = 1, shstr_used = 1, n_local = 0, n_global = 0;
    memset(strtab, 0, sizeof strtab);
    memset(shstrtab, 0, sizeof shstrtab);
    strtab[0] = 0; shstrtab[0] = 0;
    for (unsigned i = 0; i < n_simbolos; i++) {
        if (!simbolos[i].definido && !simbolos[i].global) {
            falhar(1, "simbolo nao definido nem exportado");
            return 0;
        }
        if (simbolos[i].global) n_global++;
        else if (simbolos[i].definido) n_local++;
    }
    unsigned k = 0;
    for (unsigned i = 0; i < n_simbolos; i++)
        if (simbolos[i].definido && !simbolos[i].global) ordem[k++] = i;
    unsigned first_global = 2u + k;
    for (unsigned i = 0; i < n_simbolos; i++)
        if (simbolos[i].global) ordem[k++] = i;
    unsigned n_sym = 2u + n_local + n_global;
    for (unsigned i = 0; i < n_simbolos; i++) indice_obj[i] = 0;
    for (unsigned i = 0; i < n_local + n_global; i++) {
        unsigned si = ordem[i];
        nome_off[si] = adicionar_nome(strtab, sizeof strtab, &str_used, simbolos[si].nome);
        if (nome_off[si] == 0xFFFFFFFFu) { falhar(1, "tabela ELF de nomes excede o limite"); return 0; }
        indice_obj[si] = i + 2u;
    }
    unsigned sh_text = adicionar_nome(shstrtab, sizeof shstrtab, &shstr_used, ".text");
    unsigned sh_rel = adicionar_nome(shstrtab, sizeof shstrtab, &shstr_used, ".rel.text");
    unsigned sh_sym = adicionar_nome(shstrtab, sizeof shstrtab, &shstr_used, ".symtab");
    unsigned sh_str = adicionar_nome(shstrtab, sizeof shstrtab, &shstr_used, ".strtab");
    unsigned sh_shstr = adicionar_nome(shstrtab, sizeof shstrtab, &shstr_used, ".shstrtab");
    if (sh_shstr == 0xFFFFFFFFu) { falhar(1, "tabela ELF de secoes excede o limite"); return 0; }

    unsigned off_text = alinhar(52u, 16u);
    unsigned off_rel = alinhar(off_text + tamanho_codigo, 4u);
    unsigned off_sym = alinhar(off_rel + n_relocacoes * 8u, 4u);
    unsigned off_str = off_sym + n_sym * 16u;
    unsigned off_shstr = off_str + str_used;
    unsigned off_sh = alinhar(off_shstr + shstr_used, 4u);
    unsigned total = off_sh + 6u * 40u;
    if (total > sizeof imagem || off_text + tamanho_codigo > sizeof imagem ||
        off_rel + n_relocacoes * 8u > sizeof imagem || off_sym + n_sym * 16u > sizeof imagem ||
        off_shstr + shstr_used > sizeof imagem) {
        falhar(1, "objeto ELF excede o limite de saida");
        return 0;
    }
    memset(imagem, 0, total);
    memcpy(imagem, "\177ELF", 4);
    imagem[4] = 1; imagem[5] = 1; imagem[6] = 1;
    gravar32(16, 1, 2); gravar32(18, 3, 2); gravar32(20, 1, 4);
    gravar32(32, off_sh, 4); gravar32(40, 52, 2); gravar32(42, 32, 2);
    gravar32(44, 0, 2); gravar32(46, 40, 2); gravar32(48, 6, 2); gravar32(50, 5, 2);
    memcpy(imagem + off_text, codigo, tamanho_codigo);
    for (unsigned i = 0; i < n_relocacoes; i++) {
        unsigned r = off_rel + i * 8u;
        reloc_t *rel = &relocacoes[i];
        if (rel->offset > tamanho_codigo || tamanho_codigo - rel->offset < 4 ||
            rel->simbolo >= n_simbolos || !indice_obj[rel->simbolo]) {
            falhar(1, "relocation ELF inconsistente"); return 0;
        }
        gravar32(r, rel->offset, 4);
        gravar32(r + 4, (indice_obj[rel->simbolo] << 8) | rel->tipo, 4);
    }
    /* Simbolo nulo e simbolo de secao .text. */
    gravar32(off_sym + 16 + 12, 3, 1);
    gravar32(off_sym + 16 + 14, 1, 2);
    for (unsigned i = 0; i < n_local + n_global; i++) {
        unsigned si = ordem[i], s = off_sym + (i + 2u) * 16u;
        gravar32(s, nome_off[si], 4);
        gravar32(s + 4, simbolos[si].definido ? simbolos[si].endereco : 0, 4);
        gravar32(s + 8, 0, 4);
        gravar32(s + 12, simbolos[si].global ? 0x10 : 0x00, 1);
        gravar32(s + 13, 0, 1);
        gravar32(s + 14, simbolos[si].definido ? 1 : 0, 2);
    }
    memcpy(imagem + off_str, strtab, str_used);
    memcpy(imagem + off_shstr, shstrtab, shstr_used);

#define SH(i, field, value, bytes) gravar32(off_sh + (i) * 40u + (field), (value), (bytes))
    /* Section 1: .text */
    SH(1, 0, sh_text, 4); SH(1, 4, 1, 4); SH(1, 8, 6, 4); SH(1, 12, 0, 4);
    SH(1, 16, off_text, 4); SH(1, 20, tamanho_codigo, 4); SH(1, 24, 0, 4);
    SH(1, 28, 0, 4); SH(1, 32, 16, 4); SH(1, 36, 0, 4);
    /* Section 2: .rel.text */
    SH(2, 0, sh_rel, 4); SH(2, 4, 9, 4); SH(2, 8, 0, 4); SH(2, 12, 0, 4);
    SH(2, 16, off_rel, 4); SH(2, 20, n_relocacoes * 8u, 4); SH(2, 24, 3, 4);
    SH(2, 28, 1, 4); SH(2, 32, 4, 4); SH(2, 36, 8, 4);
    /* Section 3: .symtab */
    SH(3, 0, sh_sym, 4); SH(3, 4, 2, 4); SH(3, 8, 0, 4); SH(3, 12, 0, 4);
    SH(3, 16, off_sym, 4); SH(3, 20, n_sym * 16u, 4); SH(3, 24, 4, 4);
    SH(3, 28, first_global, 4); SH(3, 32, 4, 4); SH(3, 36, 16, 4);
    /* Section 4: .strtab; section 5: .shstrtab. */
    SH(4, 0, sh_str, 4); SH(4, 4, 3, 4); SH(4, 8, 0, 4); SH(4, 12, 0, 4);
    SH(4, 16, off_str, 4); SH(4, 20, str_used, 4); SH(4, 24, 0, 4);
    SH(4, 28, 0, 4); SH(4, 32, 1, 4); SH(4, 36, 0, 4);
    SH(5, 0, sh_shstr, 4); SH(5, 4, 3, 4); SH(5, 8, 0, 4); SH(5, 12, 0, 4);
    SH(5, 16, off_shstr, 4); SH(5, 20, shstr_used, 4); SH(5, 24, 0, 4);
    SH(5, 28, 0, 4); SH(5, 32, 1, 4); SH(5, 36, 0, 4);
#undef SH
    return total;
}

static int carregar(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "asagui: fonte: %s\n", strerror(errno)); return 0; }
    unsigned n = 0;
    while (n < FONTE_MAX) {
        ssize_t r = read(fd, fonte + n, FONTE_MAX - n);
        if (r < 0) { close(fd); fprintf(stderr, "asagui: erro de leitura\n"); return 0; }
        if (!r) break;
        n += (unsigned)r;
    }
    char extra;
    ssize_t excedeu = n == FONTE_MAX ? read(fd, &extra, 1) : 0;
    close(fd);
    if (excedeu != 0) { fprintf(stderr, "asagui: fonte maior que 16 KiB\n"); return 0; }
    for (unsigned i = 0; i < n; i++) if (!fonte[i]) { fprintf(stderr, "asagui: byte NUL na fonte\n"); return 0; }
    fonte[n] = 0;
    return 1;
}

int principal(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "version")) {
        puts("asagui 0.2: assembler i386 subset -> ELF32 ET_REL/ET_EXEC");
        return 0;
    }
    const char *destino = NULL, *entrada_fonte = NULL;
    if (argc == 5 && !strcmp(argv[1], "build") && !strcmp(argv[2], "-o")) {
        destino = argv[3]; entrada_fonte = argv[4]; modo_objeto = 0;
    } else if (argc == 6 && !strcmp(argv[1], "assemble") &&
               !strcmp(argv[2], "-c") && !strcmp(argv[3], "-o")) {
        destino = argv[4]; entrada_fonte = argv[5]; modo_objeto = 1;
    } else {
        fprintf(stderr, "uso: asagui build -o saida.elf entrada.asm\n"
                        "     asagui assemble -c -o saida.o entrada.asm\n");
        return 1;
    }
    if (!strcmp(destino, entrada_fonte)) { fprintf(stderr, "asagui: origem e destino iguais\n"); return 1; }
    if (!carregar(entrada_fonte)) return 1;
    if (!pass1() || !pass2() || erro) return 1;
    unsigned total = modo_objeto ? criar_objeto() : criar_elf();
    if (!total || erro) return 1;
    int fd = open(destino, O_WRONLY | O_CREAT | O_TRUNC, modo_objeto ? 0644 : 0755);
    if (fd < 0) { fprintf(stderr, "asagui: destino: %s\n", strerror(errno)); return 1; }
    ssize_t r = escrever_tudo(fd, imagem, total);
    int f = close(fd);
    if (r < 0 || f < 0) { (void)unlink(destino); fprintf(stderr, "asagui: erro de gravacao\n"); return 1; }
    printf("asagui: %s gerado (%u bytes, codigo %u bytes%s)\n", destino, total, tamanho_codigo,
           modo_objeto ? ", ELF32 relocavel" : "");
    return 0;
}
