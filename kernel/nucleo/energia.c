/* energia.c - reiniciar e desligar a máquina.
 *
 * Reiniciar é fácil: o controlador do teclado tem uma linha ligada ao reset
 * da CPU. Desligar exige falar com a ACPI do firmware: achar a RSDP, seguir
 * até a FADT (onde está a porta PM1a_CNT), descobrir no DSDT o SLP_TYP do
 * estado S5 e escrever nele o pedido de dormir para sempre.
 *
 * O PRD proíbe código de outros sistemas, então nada de biblioteca de ACPI:
 * aqui estão só os 4 campos que o desligamento precisa, com a soma de
 * verificação de cada tabela conferida. Ver D038. */
#include "cpu.h"
#include "console.h"
#include "paginacao.h"
#include "portas.h"
#include "relogio.h"
#include "texto.h"

SEM_RETORNO void maquina_reiniciar(void)
{
    klog("[SAGUI] reiniciando\n");
    int_desliga();
    /* O controlador de teclado tem uma linha ligada ao reset da CPU: o
     * comando 0xFE dá um pulso nela. */
    for (int i = 0; i < 100000 && (porta_le8(0x64) & 2); i++) ;
    porta_escreve8(0x64, 0xFE);
    for (volatile int i = 0; i < 1000000; i++) ;
    /* Se não funcionou: IDT vazia + interrupção = falta tripla, que também reinicia. */
    struct EMPACOTADO { uint16_t limite; uint32_t base; } vazia = { 0, 0 };
    __asm__ volatile("lidt %0; int3" : : "m"(vazia));
    parar_maquina();
}

/* ------------------------------------------------------------ desligar (ACPI) */

/* Em AML, o pacote do \_S5 é montado com um byte de tamanho, um byte com a
 * quantidade de elementos e os elementos: Zero (0x00), One (0x01) ou
 * constante de 1 byte (0x0A + valor). */
static bool elemento_aml(const uint8_t *d, uint32_t n, uint32_t p, int *valor)
{
    if (p >= n) return false;
    if (d[p] == 0x00 || d[p] == 0x01) { *valor = d[p]; return true; }
    if (d[p] == 0x0A && p + 1 < n) { *valor = d[p + 1]; return true; }
    return false;
}

/* Soma de verificação de uma tabela ACPI: os bytes têm que somar zero. */
static bool soma_acpi(const uint8_t *t, uint32_t n)
{
    uint8_t soma = 0;
    for (uint32_t i = 0; i < n; i++) soma = (uint8_t)(soma + t[i]);
    return soma == 0;
}

/* Procura "nome" seguido de um pacote AML com dois valores (o \_S5 do DSDT:
 * Name(_S5_, Package(){SLP_TYPa, SLP_TYPb, 0, 0})). Sem interpretar AML
 * inteiro: o desligamento só precisa destes dois números. */
static bool s5_na_tabela(const uint8_t *t, int *typ_a, int *typ_b)
{
    if (!t || memcmp(t, "DSDT", 4) != 0) return false;
    uint32_t n = *(const uint32_t *)(t + 4);
    if (n > 256u * 1024u) return false;   /* sanidade: DSDT grande demais */
    for (uint32_t i = 0; i + 8 < n; i++) {
        if (memcmp(t + i, "_S5_", 4) != 0 || t[i + 4] != 0x12) continue;   /* PackageOp */
        uint32_t p = i + 5;
        if (t[p] & 0xC0) return false;   /* pacote com tamanho de vários bytes: fora do nosso caso */
        p++;   /* tamanho do pacote */
        p++;   /* quantidade de elementos */
        if (!elemento_aml(t, n, p, typ_a)) return false;
        int b;
        if (elemento_aml(t, n, p + 2, &b) || elemento_aml(t, n, p + 1, &b)) *typ_b = b;
        else *typ_b = 0;
        klog("[ACPI] S5 na tabela: slp_typ %d/%d\n", *typ_a, *typ_b);
        return true;
    }
    return false;
}

/* A RSDP fica na EBDA (o BIOS diz o segmento dela em 0x40E) ou entre
 * 0xE0000 e 0xFFFFF. A página 0 é mantida desligada para pegar ponteiro
 * nulo, então ela é mapeada num endereço alto por um instante, só para
 * ler o ponteiro da EBDA. Só endereços baixos são lidos: o mapeamento 1:1
 * do kernel vai até KERNEL_JANELAS. */
static bool rsdp_aqui(const uint8_t *r)
{
    return memcmp(r, "RSD PTR ", 8) == 0 && soma_acpi(r, 20) && *(const uint32_t *)(r + 16);
}

static const uint8_t *achar_rsdp(void)
{
    uint32_t base = janela_mapear(0, 8);
    if (base) {
        uint32_t ebda = (uint32_t)(*(const uint16_t *)(base + 0x40E)) << 4;
        if (ebda >= 0x1000 && ebda < 0xE0000)   /* ponteiro da EBDA com cara de verdade */
            for (uint32_t a = ebda; a < ebda + 1024; a += 16)
                if (rsdp_aqui((const uint8_t *)a)) return (const uint8_t *)a;
    }
    for (uint32_t a = 0xE0000; a <= 0x100000 - 32; a += 16)
        if (rsdp_aqui((const uint8_t *)a)) return (const uint8_t *)a;
    return NULL;
}

/* Da RSDP até a FADT: a RSDT lista as tabelas do sistema em entradas de
 * 4 bytes depois do cabeçalho padrão de 36. */
static const uint8_t *achar_fadt(const uint8_t *rsdp)
{
    uint32_t rsdt_end = *(const uint32_t *)(rsdp + 16);
    if (!rsdt_end || rsdt_end >= KERNEL_JANELAS) return NULL;
    const uint8_t *rsdt = (const uint8_t *)rsdt_end;
    if (memcmp(rsdt, "RSDT", 4) != 0) return NULL;
    uint32_t n = *(const uint32_t *)(rsdt + 4);
    if (n < 40 || n > 256u * 1024u || !soma_acpi(rsdt, n)) return NULL;
    for (uint32_t o = 36; o + 4 <= n; o += 4) {
        uint32_t end = *(const uint32_t *)(rsdt + o);
        if (!end || end >= KERNEL_JANELAS) continue;
        const uint8_t *t = (const uint8_t *)end;
        if (memcmp(t, "FACP", 4) != 0) continue;
        uint32_t tf = *(const uint32_t *)(t + 4);
        if (tf < 128 || tf > 1024 || !soma_acpi(t, tf)) continue;
        return t;
    }
    return NULL;
}

/* Escreve o pedido do estado S5: bit 13 (SLP_EN) e o SLP_TYP nos bits 10-12
 * da porta de controle PM1a (e PM1b, quando a placa tem segunda). */
static void dormir_s5(uint32_t porta, int typ)
{
    porta_escreve16((uint16_t)porta, (uint16_t)((typ & 7) << 10 | 1u << 13));
}

SEM_RETORNO void maquina_desligar(void)
{
    klog("[SAGUI] desligando\n");
    int_desliga();

    const uint8_t *rsdp = achar_rsdp();
    if (!rsdp) {
        klog("[ACPI] RSDP nao encontrada\n");
        /* Sem ACPI: só restam as portas que os emuladores usam. */
        porta_escreve16(0x604, 0x2000);    /* QEMU: PM1a_CNT, SLP_TYP 0 */
        porta_escreve16(0x4004, 0x3400);    /* VirtualBox: PM1a_CNT, SLP_TYP 5 */
        for (volatile int i = 0; i < 1000000; i++) ;
    } else {
        const uint8_t *fadt = achar_fadt(rsdp);
        if (!fadt) {
            klog("[ACPI] RSDP em %p mas sem FADT\n", rsdp);
        } else {
            uint32_t pm1a = *(const uint32_t *)(fadt + 72);   /* PM1a_CNT_BLK */
            uint32_t pm1b = *(const uint32_t *)(fadt + 76);   /* PM1b_CNT_BLK */
            int typ_a = 0, typ_b = 0;
            bool s5 = s5_na_tabela((const uint8_t *)(*(const uint32_t *)(fadt + 40)), &typ_a, &typ_b);
            klog("[ACPI] RSDP %p FADT %p pm1a %x pm1b %x%s\n", rsdp, fadt, pm1a, pm1b,
                 s5 ? " com _S5" : "");
            if (s5) {
                if (pm1a) dormir_s5(pm1a, typ_a);
                if (pm1b) dormir_s5(pm1b, typ_b);
                for (volatile int i = 0; i < 1000000; i++) ;
            } else if (pm1a == 0x604) {
                klog("[ACPI] sem _S5: valor do QEMU na porta %x\n", pm1a);
                porta_escreve16(0x604, 0x2000);
                for (volatile int i = 0; i < 1000000; i++) ;
            } else if (pm1a == 0x4004) {
                klog("[ACPI] sem _S5: valor do VirtualBox na porta %x\n", pm1a);
                porta_escreve16(0x4004, 0x3400);
                for (volatile int i = 0; i < 1000000; i++) ;
            }
        }
    }

    /* Nada cortou a energia (BIOS sem ACPI ou com um \_S5 diferente): para a
     * CPU e avisa na tela que o desligamento tem de ser no botão. */
    console_esconder(false);   /* a mensagem aparece mesmo com a área de trabalho no ar */
    console_cor(COR_VERDE_CLARO, COR_VERDE);
    console_limpar();
    kprintf("\n  O SaguiOS terminou.\n\n");
    console_cor(COR_BRANCO, COR_VERDE);
    kprintf("  Não consegui cortar a energia pela ACPI deste\n");
    kprintf("  computador: pode desligar no botão do gabinete.\n");
    parar_maquina();
}
