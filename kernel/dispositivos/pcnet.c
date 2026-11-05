/* pcnet.c - driver Ethernet polling para a AMD PCnet-PCI / PCnet-FAST III.
 *
 * Esta é a placa que o VirtualBox escolhe sozinho quando a máquina virtual é
 * criada como "Outro/Desconhecido" (justamente o tipo usado para um sistema
 * operacional caseiro), e é também a -device pcnet do QEMU. Sem este driver, o
 * SaguiOS só tinha rede se alguém trocasse a placa da máquina para
 * "Intel PRO/1000 MT Desktop".
 *
 * O controlador é acessado por portas de E/S (BAR0 é espaço de E/S, não MMIO),
 * em acessos de 16 bits:
 *   0x10 RDP  - dados do registrador escolhido em RAP
 *   0x12 RAP  - número do registrador (CSR)
 *   0x14 RST  - ler esta porta reinicia o controlador
 *   0x16 BDP  - dados dos registradores de configuração do barramento (BCR)
 *
 * O driver usa o "estilo de software 32 bits" (BCR20 = 0x0102), com anéis de
 * descritores de 16 bytes e buffers de 2 KB, tudo em memória física abaixo de
 * 4 GiB. Sem IRQ: quem chama faz polling.
 */
#include "rede.h"
#include "pci.h"
#include "portas.h"
#include "console.h"
#include "texto.h"
#include "relogio.h"

#define RX_N 16
#define TX_N 16
#define TAM_BUFFER 2048

/* Portas do controlador (modo "word I/O", o padrão depois do reset). */
#define PC_RDP 0x10
#define PC_RAP 0x12
#define PC_RST 0x14
#define PC_BDP 0x16

/* CSR0: o registrador de comando e estado. */
#define CSR0_INIT 0x0001
#define CSR0_STRT 0x0002
#define CSR0_STOP 0x0004
#define CSR0_TDMD 0x0008
#define CSR0_IDON 0x0100
#define CSR0_ERR  0x8000

/* BCR20: estilo de software (bit 8 = SSIZE32, bits 0-7 = SWSTYLE). */
#define BCR_SWS   20
#define BCR_LNKST 4
#define SWS_32BITS 0x0102

/* Descritor de transmissão (32 bits): estado com OWN = "do hardware". */
#define DESC_OWN 0x8000
#define DESC_ERRO 0x4000
#define DESC_STP 0x0200
#define DESC_ENP 0x0100

typedef struct EMPACOTADO {
    uint16_t modo;
    uint8_t  rlen;              /* log2(RX_N) << 4 */
    uint8_t  tlen;              /* log2(TX_N) << 4 */
    uint8_t  padr[6];           /* nosso MAC */
    uint16_t reservado;
    uint16_t ladrf[4];          /* filtro de multicast (0 = nenhum) */
    uint32_t rdra;              /* anel de recepção */
    uint32_t tdra;              /* anel de transmissão */
} init_bloco_t;

typedef struct EMPACOTADO {
    uint32_t base;
    uint16_t tamanho;           /* complemento de dois do tamanho do buffer */
    uint16_t status;
    uint32_t msg;               /* tamanho recebido (o hardware preenche) */
    uint32_t reservado;
} rmd_t;

typedef struct EMPACOTADO {
    uint32_t base;
    uint16_t tamanho;           /* 0xF000 | bytes */
    uint16_t status;
    uint32_t misc;
    uint32_t reservado;
} tmd_t;

static volatile init_bloco_t inicial ALINHADO(16);
static volatile rmd_t rxd[RX_N] ALINHADO(16);
static volatile tmd_t txd[TX_N] ALINHADO(16);
static uint8_t rxbuf[RX_N][TAM_BUFFER] ALINHADO(16);
static uint8_t txbuf[TX_N][TAM_BUFFER] ALINHADO(16);

static uint16_t io;
static uint8_t mac[6];
static uint32_t rxi, txi;
static bool ativo;
static uint16_t fabricante_pci, modelo_pci;

static void pausa(void) { __asm__ volatile("pause"); }

static void rap(uint16_t r) { porta_escreve16(io + PC_RAP, r); }
static uint16_t csr_le(uint16_t r) { rap(r); return porta_le16(io + PC_RDP); }
static void csr_escreve(uint16_t r, uint16_t v) { rap(r); porta_escreve16(io + PC_RDP, v); }
static uint16_t bcr_le(uint16_t r) { rap(r); return porta_le16(io + PC_BDP); }
static void bcr_escreve(uint16_t r, uint16_t v) { rap(r); porta_escreve16(io + PC_BDP, v); }

/* Espera até "ms" milissegundos por (csr_le(reg) & mascara) == esperado. */
static bool esperar(uint16_t reg, uint16_t mascara, uint16_t esperado, uint32_t ms)
{
    uint32_t fim = relogio_tiques() + ms / 10u + 1u;
    uint32_t voltas = 0;
    while ((csr_le(reg) & mascara) != esperado) {
        if ((int32_t)(relogio_tiques() - fim) >= 0 || ++voltas > 5000000u) return false;
        pausa();
    }
    return true;
}

uint16_t pcnet_ids(uint16_t *fabricante, uint16_t *modelo)
{
    if (fabricante) *fabricante = fabricante_pci;
    if (modelo) *modelo = modelo_pci;
    return ativo ? 1 : 0;
}

bool pcnet_iniciar(void)
{
    pci_disp_t p[16];
    int n = pci_procurar(0x02, 0x00, 0x00, p, 16);
    pci_disp_t *d = NULL;
    for (int i = 0; i < n; ++i)
        if (p[i].fabricante == 0x1022 && (p[i].modelo == 0x2000 || p[i].modelo == 0x2001))
            d = &p[i];
    if (!d) return false;
    fabricante_pci = d->fabricante;
    modelo_pci = d->modelo;

    /* Espaço de E/S (bit 0) e bus master (bit 2). */
    uint32_t cmd = pci_le32(d, 0x04);
    pci_escreve32(d, 0x04, (cmd & 0xFFFF0000u) | ((cmd & 0xFFFFu) | 0x0005u));
    uint32_t bar = pci_le32(d, 0x10);
    if (!(bar & 1u)) return false;              /* precisa BAR de E/S */
    io = (uint16_t)(bar & 0xFFE0u);

    /* Ler a porta de reset reinicia o controlador; o chip volta com STOP=1. */
    (void)porta_le16(io + PC_RST);
    if (!esperar(0, CSR0_STOP, CSR0_STOP, 200)) return false;

    /* Estilo de software 32 bits (só pode ser escrito com o chip parado). */
    bcr_escreve(BCR_SWS, SWS_32BITS);

    /* O MAC vem da EEPROM para os CSR12/13/14 no reset. */
    uint32_t m0 = csr_le(12), m1 = csr_le(13), m2 = csr_le(14);
    mac[0] = (uint8_t)m0;         mac[1] = (uint8_t)(m0 >> 8);
    mac[2] = (uint8_t)m1;         mac[3] = (uint8_t)(m1 >> 8);
    mac[4] = (uint8_t)m2;         mac[5] = (uint8_t)(m2 >> 8);
    if ((mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) == 0) {
        mac[0] = 0x52; mac[1] = 0x54; mac[2] = 0x00;
        mac[3] = 0x12; mac[4] = 0x34; mac[5] = 0x56;
    }

    /* Bloco de inicialização: modo promíscuo (aceita tudo), 16 descritores
     * em cada anel (log2 = 4), filtro de multicast vazio. */
    memset((void *)&inicial, 0, sizeof inicial);
    inicial.modo = 0x8000;                      /* PROM */
    inicial.rlen = (uint8_t)(4 << 4);
    inicial.tlen = (uint8_t)(4 << 4);
    for (int i = 0; i < 6; i++) inicial.padr[i] = mac[i];
    inicial.rdra = (uint32_t)(uintptr_t)&rxd[0];
    inicial.tdra = (uint32_t)(uintptr_t)&txd[0];

    for (uint32_t i = 0; i < RX_N; i++) {
        rxd[i].base = (uint32_t)(uintptr_t)&rxbuf[i][0];
        rxd[i].tamanho = (uint16_t)(0u - TAM_BUFFER);   /* complemento de dois */
        rxd[i].msg = 0;
        rxd[i].status = DESC_OWN;                       /* livre para o chip */
    }
    for (uint32_t i = 0; i < TX_N; i++) {
        txd[i].base = (uint32_t)(uintptr_t)&txbuf[i][0];
        txd[i].tamanho = 0;
        txd[i].status = 0;
        txd[i].misc = 0;
    }

    csr_escreve(1, (uint16_t)((uint32_t)(uintptr_t)&inicial & 0xFFFF));
    csr_escreve(2, (uint16_t)((uint32_t)(uintptr_t)&inicial >> 16));
    csr_escreve(3, 0);        /* sem troca de bytes, endereçamento de 32 bits */
    csr_escreve(4, 0);        /* sem contadores automáticos, sem "poll" */
    csr_escreve(15, 0);       /* modo normal: acrescenta o CRC ao transmitir */

    csr_escreve(0, CSR0_INIT);
    if (!esperar(0, CSR0_IDON, CSR0_IDON, 200)) return false;
    csr_escreve(0, CSR0_STRT);          /* liga transmissão e recepção */

    if (csr_le(0) & CSR0_ERR) {         /* erro de inicialização */
        csr_escreve(0, CSR0_ERR);
        return false;
    }
    rxi = 0; txi = 0; ativo = true;

    kprintf("Rede: AMD PCnet %x:%x, MAC %x:%x:%x:%x:%x:%x (Ethernet polling)\n",
            d->fabricante, d->modelo, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
}

bool pcnet_link(void)
{
    if (!ativo) return false;
    uint16_t lnkst = bcr_le(BCR_LNKST);
    return (lnkst & 0x0040u) != 0 || (lnkst & 0x8000u) != 0;
}

const uint8_t *pcnet_mac(void) { return mac; }

bool pcnet_enviar(const void *quadro, uint16_t bytes)
{
    if (!ativo || !quadro || bytes < 14 || bytes > TAM_BUFFER) return false;
    volatile tmd_t *d = &txd[txi];
    /* Descarta o descritor só depois de o chip terminar o anterior. */
    uint32_t fim = relogio_tiques() + 10u;
    while (d->status & DESC_OWN) {
        if ((int32_t)(relogio_tiques() - fim) >= 0) return false;
        pausa();
    }
    const uint8_t *s = (const uint8_t *)quadro;
    for (uint32_t i = 0; i < bytes; i++) txbuf[txi][i] = s[i];
    d->base = (uint32_t)(uintptr_t)&txbuf[txi][0];
    d->tamanho = (uint16_t)(0xF000u | bytes);   /* o alto é o complemento */
    d->misc = 0;
    __asm__ volatile("" ::: "memory");
    d->status = DESC_OWN | DESC_STP | DESC_ENP;
    csr_escreve(0, (uint16_t)(csr_le(0) | CSR0_TDMD));   /* "pode transmitir" */

    fim = relogio_tiques() + 10u;
    while (d->status & DESC_OWN) {
        if ((int32_t)(relogio_tiques() - fim) >= 0) return false;
        pausa();
    }
    txi = (txi + 1) % TX_N;
    return (d->status & DESC_ERRO) == 0;
}

uint16_t pcnet_receber(void *quadro, uint16_t capacidade)
{
    if (!ativo || !quadro || !capacidade) return 0;
    volatile rmd_t *d = &rxd[rxi];
    if (d->status & DESC_OWN) return 0;         /* o chip ainda não entregou */
    uint16_t n = (uint16_t)(d->msg & 0x0FFFu);
    if ((d->status & DESC_ERRO) || n > TAM_BUFFER || n > capacidade) n = 0;
    uint8_t *dst = (uint8_t *)quadro;
    for (uint16_t i = 0; i < n; i++) dst[i] = rxbuf[rxi][i];
    /* Devolve o descritor: primeiro o tamanho, depois o OWN. */
    d->tamanho = (uint16_t)(0u - TAM_BUFFER);
    d->msg = 0;
    __asm__ volatile("" ::: "memory");
    d->status = DESC_OWN;
    rxi = (rxi + 1) % RX_N;
    return n;
}
