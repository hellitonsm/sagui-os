/* e1000.c - driver Ethernet polling para Intel 82540EM (QEMU e1000).
 *
 * O driver usa anéis DMA estáticos, abaixo de 4 GiB, coerentes com o modelo
 * DMA do dispositivo. BAR0 é mapeado pela janela MMIO do kernel. Sem IRQ:
 * os chamadores fazem polling.
 */
#include "rede.h"
#include "pci.h"
#include "portas.h"
#include "console.h"
#include "texto.h"
#include "paginacao.h"

#define RX_N 16
#define TX_N 16
#define QUADRO_MAX 2048
#define REG_CTRL 0x0000
#define REG_STATUS 0x0008
#define REG_EECD 0x0010
#define REG_ICR 0x00C0
#define REG_IMC 0x00D8
#define REG_RCTL 0x0100
#define REG_TCTL 0x0400
#define REG_TIPG 0x0410
#define REG_RDBAL 0x2800
#define REG_RDBAH 0x2804
#define REG_RDLEN 0x2808
#define REG_RDH 0x2810
#define REG_RDT 0x2818
#define REG_TDBAL 0x3800
#define REG_TDBAH 0x3804
#define REG_TDLEN 0x3808
#define REG_TDH 0x3810
#define REG_TDT 0x3818
#define REG_RAL 0x5400
#define REG_RAH 0x5404
#define CTRL_RST 0x04000000u
#define CTRL_SLU 0x00000040u
#define RX_EN 0x00000002u
#define RX_SBP 0x00000004u
#define RX_UPE 0x00000008u
#define RX_MPE 0x00000010u
#define RX_BAM 0x00008000u
#define RX_SECRC 0x04000000u
#define TX_EN 0x00000002u
#define TX_PSP 0x00000008u
#define TX_COLD 0x00040000u
#define TX_CRS 0x00080000u
#define DESC_DD 0x01u
#define DESC_EOP 0x01u
#define DESC_IFCS 0x02u
#define DESC_RS 0x08u

typedef struct EMPACOTADO { uint32_t addr_lo, addr_hi; uint16_t length; uint8_t csum, command, status, csum_offset; uint16_t special; } tx_desc_t;
typedef struct EMPACOTADO { uint32_t addr_lo, addr_hi; uint16_t length; uint16_t csum; uint8_t status, errors; uint16_t special; } rx_desc_t;

static volatile tx_desc_t tx[TX_N] ALINHADO(16);
static volatile rx_desc_t rx[RX_N] ALINHADO(16);
static uint8_t txbuf[TX_N][QUADRO_MAX] ALINHADO(16);
static uint8_t rxbuf[RX_N][QUADRO_MAX] ALINHADO(16);
static volatile uint32_t *mmio;
static uint8_t mac[6];
static uint32_t txi, rxi;
static bool ativo;

static uint32_t ler(uint32_t r) { return mmio[r >> 2]; }
static void gravar(uint32_t r, uint32_t v) { mmio[r >> 2] = v; }
static void espera(void) { for (volatile uint32_t i=0; i<1000000; ++i) __asm__ volatile("pause"); }

bool e1000_iniciar(void)
{
    pci_disp_t p[16];
    int n = pci_procurar(0x02, 0x00, 0x00, p, 16);
    pci_disp_t *d = NULL;
    for (int i=0; i<n; ++i)
        if (p[i].fabricante == 0x8086 && (p[i].modelo == 0x100E || p[i].modelo == 0x100F || p[i].modelo == 0x10D3)) { d=&p[i]; break; }
    if (!d) return false;

    /* Habilita decodificação MMIO e bus mastering no command register. */
    uint32_t cmd = pci_le32(d, 0x04);
    pci_escreve32(d, 0x04, (cmd & 0xFFFF0000u) | ((cmd & 0xFFFFu) | 0x0006u));
    uint32_t bar = pci_le32(d, 0x10);
    if ((bar & 1u) || (bar & 0xFFFFFFF0u) == 0) return false; /* precisa BAR MMIO */
    uint32_t base = bar & 0xFFFFFFF0u;
    uint32_t janela = janela_mapear(base, 0x6000u);
    if (!janela) return false;
    mmio = (volatile uint32_t *)janela;

    gravar(REG_IMC, 0xFFFFFFFFu); (void)ler(REG_ICR);
    gravar(REG_CTRL, ler(REG_CTRL) | CTRL_RST); espera();
    gravar(REG_IMC, 0xFFFFFFFFu); (void)ler(REG_ICR);
    gravar(REG_CTRL, ler(REG_CTRL) | CTRL_SLU);

    uint32_t lo = ler(REG_RAL), hi = ler(REG_RAH);
    if (!(hi & 0x80000000u)) {
        /* EEPROM serial do QEMU e dos 8254x: lê palavras de MAC. */
        uint32_t ee = ler(REG_EECD);
        for (volatile int t=0; !(ee & 0x10u) && t<100000; ++t) { __asm__ volatile("pause"); ee=ler(REG_EECD); }
    }
    mac[0]=(uint8_t)lo; mac[1]=(uint8_t)(lo>>8); mac[2]=(uint8_t)(lo>>16); mac[3]=(uint8_t)(lo>>24);
    mac[4]=(uint8_t)hi; mac[5]=(uint8_t)(hi>>8);
    if ((mac[0]|mac[1]|mac[2]|mac[3]|mac[4]|mac[5]) == 0) return false;

    for (uint32_t i=0; i<RX_N; ++i) {
        rx[i].addr_lo=(uint32_t)(uintptr_t)&rxbuf[i][0]; rx[i].addr_hi=0;
        rx[i].status=0; rx[i].length=0;
    }
    for (uint32_t i=0; i<TX_N; ++i) {
        tx[i].addr_lo=(uint32_t)(uintptr_t)&txbuf[i][0]; tx[i].addr_hi=0;
        tx[i].status=DESC_DD; tx[i].command=0;
    }
    gravar(REG_RDBAL,(uint32_t)(uintptr_t)&rx[0]); gravar(REG_RDBAH,0);
    gravar(REG_RDLEN,sizeof(rx)); gravar(REG_RDH,0); gravar(REG_RDT,RX_N-1);
    gravar(REG_TDBAL,(uint32_t)(uintptr_t)&tx[0]); gravar(REG_TDBAH,0);
    gravar(REG_TDLEN,sizeof(tx)); gravar(REG_TDH,0); gravar(REG_TDT,0);
    gravar(REG_RCTL,RX_EN|RX_BAM|RX_SECRC); /* buffer padrão 2048; descarta CRC */
    gravar(REG_TCTL,TX_EN|TX_PSP|TX_COLD|TX_CRS);
    gravar(REG_TIPG,0x0060200Au);
    txi=0; rxi=0; ativo=true;
    kprintf("Rede: Intel %x:%x, MAC %x:%x:%x:%x:%x:%x (Ethernet polling)\n",
            d->fabricante,d->modelo,mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    return true;
}

bool e1000_disponivel(void) { return ativo; }
const uint8_t *e1000_mac(void) { return mac; }

bool e1000_enviar(const void *quadro, uint16_t bytes)
{
    if (!ativo || !quadro || bytes < 14 || bytes > QUADRO_MAX) return false;
    volatile tx_desc_t *d=&tx[txi];
    /* Espera até 100 ms pelo descritor: evita travar o kernel para sempre. */
    uint32_t limite=100000;
    while (!(d->status & DESC_DD) && limite--) __asm__ volatile("pause");
    if (!(d->status & DESC_DD)) return false;
    const uint8_t *s=(const uint8_t *)quadro;
    for (uint32_t i=0;i<bytes;i++) txbuf[txi][i]=s[i];
    d->length=bytes; d->csum=0; d->status=0; d->command=DESC_EOP|DESC_IFCS|DESC_RS;
    uint32_t prox=(txi+1)%TX_N;
    gravar(REG_TDT,prox); txi=prox;
    return true;
}

uint16_t e1000_receber(void *quadro, uint16_t capacidade)
{
    if (!ativo || !quadro || !capacidade) return 0;
    volatile rx_desc_t *d=&rx[rxi];
    if (!(d->status & DESC_DD)) return 0;
    uint16_t n=d->length;
    if (d->errors || n>QUADRO_MAX || n>capacidade) n=0;
    uint8_t *dst=(uint8_t *)quadro;
    for (uint32_t i=0;i<n;i++) dst[i]=rxbuf[rxi][i];
    d->length=0; d->status=0; d->errors=0;
    gravar(REG_RDT,rxi);
    rxi=(rxi+1)%RX_N;
    return n;
}
