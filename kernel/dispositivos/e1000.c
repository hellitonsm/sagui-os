/* e1000.c - driver Ethernet polling para a família Intel 8254x.
 *
 * Cobre as três placas que o VirtualBox oferece como "Intel PRO/1000":
 *   82540EM (MT Desktop, id 0x100E) - o padrão das máquinas modernas;
 *   82545EM (MT Server,    id 0x100F);
 *   82543GC (T  Server,    id 0x1004) - o antigo, usado em máquinas "2000".
 * No QEMU é a -device e1000 (id 0x100E). O 82574L (0x10D3) também é aceito.
 *
 * O que o VirtualBox exige e o QEMU perdoava (e que a versão anterior deste
 * driver não fazia):
 *   - esperar o fim do reset lendo CTRL.RST, em vez de contar voltas fixas.
 *     Sem isso o CTRL.SLU podia ser escrito com o reset ainda em curso e o
 *     link nunca subia;
 *   - gravar de volta RAL/RAH (com o bit AV), porque é essa tabela que o
 *     controlador usa para filtrar os quadros que aceita;
 *   - ligar os bits de promíscuo (UPE/MPE), que é como os drivers de verdade
 *     fazem quando querem receber tudo — ARP, DHCP e broadcast inclusos;
 *   - ler o MAC da EEPROM quando os registradores vêm zerados (acontece em
 *     algumas versões do VirtualBox antes do primeiro reset completo).
 *
 * Sem IRQ: o driver usa anéis DMA estáticos e os chamadores fazem polling.
 */
#include "rede.h"
#include "pci.h"
#include "portas.h"
#include "console.h"
#include "texto.h"
#include "paginacao.h"
#include "relogio.h"

#define RX_N 16
#define TX_N 16
#define QUADRO_MAX 2048

#define REG_CTRL   0x0000
#define REG_STATUS 0x0008
#define REG_EECD   0x0010
#define REG_EERD   0x0014
#define REG_ICR    0x00C0
#define REG_IMC    0x00D8
#define REG_RCTL   0x0100
#define REG_TCTL   0x0400
#define REG_TIPG   0x0410
#define REG_RDBAL  0x2800
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818
#define REG_TDBAL  0x3800
#define REG_TDBAH  0x3804
#define REG_TDLEN  0x3808
#define REG_TDH    0x3810
#define REG_TDT    0x3818
#define REG_RAL    0x5400
#define REG_RAH    0x5404

#define CTRL_RST   0x04000000u
#define CTRL_SLU   0x00000040u
#define CTRL_ASDE  0x00000020u
#define STATUS_LU  0x00000002u

#define RX_EN      0x00000002u
#define RX_SBP     0x00000004u
#define RX_UPE     0x00000008u
#define RX_MPE     0x00000010u
#define RX_BAM     0x00008000u
#define RX_SECRC   0x04000000u
#define TX_EN      0x00000002u
#define TX_PSP     0x00000008u
#define TX_COLD    0x00040000u
#define TX_CRS     0x00080000u

#define DESC_DD    0x01u
#define DESC_EOP   0x01u
#define DESC_IFCS  0x02u
#define DESC_RS    0x08u

typedef struct EMPACOTADO {
    uint32_t addr_lo, addr_hi;
    uint16_t length;
    uint8_t  csum, command, status, csum_offset;
    uint16_t special;
} tx_desc_t;

typedef struct EMPACOTADO {
    uint32_t addr_lo, addr_hi;
    uint16_t length;
    uint16_t csum;
    uint8_t  status, errors;
    uint16_t special;
} rx_desc_t;

static volatile tx_desc_t tx[TX_N] ALINHADO(16);
static volatile rx_desc_t rx[RX_N] ALINHADO(16);
static uint8_t txbuf[TX_N][QUADRO_MAX] ALINHADO(16);
static uint8_t rxbuf[RX_N][QUADRO_MAX] ALINHADO(16);
static volatile uint32_t *mmio;
static uint8_t mac[6];
static uint32_t txi, rxi;
static bool ativo;
static uint16_t fabricante_pci, modelo_pci;

static uint32_t ler(uint32_t r) { return mmio[r >> 2]; }
static void gravar(uint32_t r, uint32_t v) { mmio[r >> 2] = v; }
static void pausa(void) { __asm__ volatile("pause"); }

/* Espera um registrador ficar com (valor & mascara) == esperado. */
static bool esperar_reg(uint32_t reg, uint32_t mascara, uint32_t esperado, uint32_t ms)
{
    uint32_t fim = relogio_tiques() + ms / 10u + 1u;
    uint32_t voltas = 0;
    while ((ler(reg) & mascara) != esperado) {
        if ((int32_t)(relogio_tiques() - fim) >= 0 || ++voltas > 5000000u) return false;
        pausa();
    }
    return true;
}

/* Lê uma palavra da EEPROM pelo registrador EERD (0x0014). */
static bool eeprom_ler(uint8_t endereco, uint16_t *valor)
{
    gravar(REG_EERD, ((uint32_t)endereco << 8) | 1u);
    for (uint32_t i = 0; i < 1000000u; i++) {
        uint32_t v = ler(REG_EERD);
        if (v & 2u) { *valor = (uint16_t)(v >> 16); return true; }   /* DONE */
        pausa();
    }
    return false;
}

static bool id_conhecido(uint16_t modelo)
{
    return modelo == 0x100E || modelo == 0x100F || modelo == 0x1004 || modelo == 0x10D3;
}

uint16_t e1000_ids(uint16_t *fabricante, uint16_t *modelo)
{
    if (fabricante) *fabricante = fabricante_pci;
    if (modelo) *modelo = modelo_pci;
    return ativo ? 1 : 0;
}

bool e1000_iniciar(void)
{
    pci_disp_t p[16];
    int n = pci_procurar(0x02, 0x00, 0x00, p, 16);
    pci_disp_t *d = NULL;
    for (int i = 0; i < n; ++i)
        if (p[i].fabricante == 0x8086 && id_conhecido(p[i].modelo)) {
            /* Prefere a 82540EM (a placa padrão do VirtualBox) se houver mais
             * de uma placa Intel na máquina. */
            if (!d || p[i].modelo == 0x100E) d = &p[i];
            if (p[i].modelo == 0x100E) break;
        }
    if (!d) return false;
    fabricante_pci = d->fabricante;
    modelo_pci = d->modelo;

    /* Liga a decodificação de MMIO (bit 1) e o bus master (bit 2). */
    uint32_t cmd = pci_le32(d, 0x04);
    pci_escreve32(d, 0x04, (cmd & 0xFFFF0000u) | ((cmd & 0xFFFFu) | 0x0006u));
    uint32_t bar = pci_le32(d, 0x10);
    if ((bar & 1u) || (bar & 0xFFFFFFF0u) == 0) return false;   /* precisa BAR MMIO */
    uint32_t base = bar & 0xFFFFFFF0u;
    uint32_t janela = janela_mapear(base, 0x6000u);
    if (!janela) return false;
    mmio = (volatile uint32_t *)janela;

    /* Reset: pede e espera o controlador limpar o bit sozinho. */
    gravar(REG_IMC, 0xFFFFFFFFu); (void)ler(REG_ICR);
    gravar(REG_CTRL, ler(REG_CTRL) | CTRL_RST);
    if (!esperar_reg(REG_CTRL, CTRL_RST, 0, 200)) return false;
    gravar(REG_IMC, 0xFFFFFFFFu); (void)ler(REG_ICR);
    /* SLU = "set link up"; ASDE deixa a auto-negociação no comando do chip. */
    gravar(REG_CTRL, ler(REG_CTRL) | CTRL_SLU | CTRL_ASDE);

    /* MAC: primeiro os registradores (RAL/RAH), depois a EEPROM. */
    bool mac_ok = false;
    for (int volta = 0; volta < 50 && !mac_ok; volta++) {
        uint32_t lo = ler(REG_RAL), hi = ler(REG_RAH);
        mac[0] = (uint8_t)lo;       mac[1] = (uint8_t)(lo >> 8);
        mac[2] = (uint8_t)(lo >> 16); mac[3] = (uint8_t)(lo >> 24);
        mac[4] = (uint8_t)hi;       mac[5] = (uint8_t)(hi >> 8);
        mac_ok = (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) != 0;
        if (!mac_ok) { uint32_t ee = ler(REG_EECD); (void)ee; pausa(); }
    }
    if (!mac_ok) {
        uint16_t w0 = 0, w1 = 0, w2 = 0;
        if (eeprom_ler(0, &w0) && eeprom_ler(1, &w1) && eeprom_ler(2, &w2)) {
            mac[0] = (uint8_t)w0; mac[1] = (uint8_t)(w0 >> 8);
            mac[2] = (uint8_t)w1; mac[3] = (uint8_t)(w1 >> 8);
            mac[4] = (uint8_t)w2; mac[5] = (uint8_t)(w2 >> 8);
        }
    }
    if ((mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) == 0) {
        /* Último recurso: um MAC local (o bit de "administrado localmente"
         * ligado) para a placa não ficar muda. */
        mac[0] = 0x52; mac[1] = 0x54; mac[2] = 0x00;
        mac[3] = 0x12; mac[4] = 0x34; mac[5] = 0x57;
    }
    /* Grava a tabela de endereços de recepção: é ela que o 8254x (e o
     * VirtualBox) consultam para aceitar um quadro. RAH.AV = endereço válido. */
    gravar(REG_RAL, (uint32_t)mac[0] | ((uint32_t)mac[1] << 8) |
                    ((uint32_t)mac[2] << 16) | ((uint32_t)mac[3] << 24));
    gravar(REG_RAH, ((uint32_t)mac[4] | ((uint32_t)mac[5] << 8)) | 0x80000000u);

    for (uint32_t i = 0; i < RX_N; ++i) {
        rx[i].addr_lo = (uint32_t)(uintptr_t)&rxbuf[i][0];
        rx[i].addr_hi = 0;
        rx[i].status = 0;
        rx[i].length = 0;
    }
    for (uint32_t i = 0; i < TX_N; ++i) {
        tx[i].addr_lo = (uint32_t)(uintptr_t)&txbuf[i][0];
        tx[i].addr_hi = 0;
        tx[i].status = DESC_DD;    /* livre */
        tx[i].command = 0;
    }
    gravar(REG_RDBAL, (uint32_t)(uintptr_t)&rx[0]); gravar(REG_RDBAH, 0);
    gravar(REG_RDLEN, sizeof(rx)); gravar(REG_RDH, 0); gravar(REG_RDT, RX_N - 1);
    gravar(REG_TDBAL, (uint32_t)(uintptr_t)&tx[0]); gravar(REG_TDBAH, 0);
    gravar(REG_TDLEN, sizeof(tx)); gravar(REG_TDH, 0); gravar(REG_TDT, 0);
    /* Buffer de 2048 (BSIZE = 0), aceita tudo (UPE/MPE/BAM), tira o CRC. */
    gravar(REG_RCTL, RX_EN | RX_BAM | RX_UPE | RX_MPE | RX_SECRC);
    gravar(REG_TCTL, TX_EN | TX_PSP | TX_COLD | TX_CRS);
    gravar(REG_TIPG, 0x0060200Au);
    txi = 0; rxi = 0; ativo = true;

    kprintf("Rede: Intel %x:%x, MAC %x:%x:%x:%x:%x:%x (Ethernet polling)\n",
            d->fabricante, d->modelo, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
}

bool e1000_link(void)
{
    if (!ativo) return false;
    return (ler(REG_STATUS) & STATUS_LU) != 0;
}

const uint8_t *e1000_mac(void) { return mac; }

bool e1000_enviar(const void *quadro, uint16_t bytes)
{
    if (!ativo || !quadro || bytes < 14 || bytes > QUADRO_MAX) return false;
    volatile tx_desc_t *d = &tx[txi];
    /* Espera até 100 ms pelo descritor: evita travar o kernel para sempre. */
    uint32_t fim = relogio_tiques() + 10u;
    while (!(d->status & DESC_DD)) {
        if ((int32_t)(relogio_tiques() - fim) >= 0) return false;
        pausa();
    }
    const uint8_t *s = (const uint8_t *)quadro;
    for (uint32_t i = 0; i < bytes; i++) txbuf[txi][i] = s[i];
    d->length = bytes; d->csum = 0; d->status = 0;
    d->command = DESC_EOP | DESC_IFCS | DESC_RS;
    uint32_t prox = (txi + 1) % TX_N;
    gravar(REG_TDT, prox); txi = prox;
    return true;
}

uint16_t e1000_receber(void *quadro, uint16_t capacidade)
{
    if (!ativo || !quadro || !capacidade) return 0;
    volatile rx_desc_t *d = &rx[rxi];
    if (!(d->status & DESC_DD)) return 0;
    uint16_t n = d->length;
    if (d->errors || n > QUADRO_MAX || n > capacidade) n = 0;
    uint8_t *dst = (uint8_t *)quadro;
    for (uint32_t i = 0; i < n; i++) dst[i] = rxbuf[rxi][i];
    d->length = 0; d->status = 0; d->errors = 0;
    gravar(REG_RDT, rxi);
    rxi = (rxi + 1) % RX_N;
    return n;
}
