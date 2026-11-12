/* rede.c - a placa de rede escolhida e a configuração IP do sistema.
 *
 * Ordem de procura (a mesma que o VirtualBox e o QEMU oferecem):
 *   1. Intel 8254x ("Intel PRO/1000 MT Desktop" no VirtualBox, -device e1000
 *      no QEMU);
 *   2. AMD PCnet-PCI/FAST III (placa padrão do VirtualBox para máquinas de
 *      tipo "Outro", -device pcnet no QEMU).
 *
 * Depois de achar a placa, o endereço vem de DHCP. A rede NAT do VirtualBox e
 * a user-net do QEMU usam o mesmo esquema (10.0.2.0/24, gateway 10.0.2.2,
 * DNS 10.0.2.3); se o DHCP não responder, é esse o endereço que fica valendo,
 * o que mantém o `wget` funcionando nas duas máquinas virtuais sem ninguém
 * configurar nada. Em rede "Bridge" ou "Host-only" do VirtualBox o DHCP
 * responde e o endereço passa a ser o da rede real.
 */
#include "rede.h"
#include "dhcp.h"
#include "pacotes.h"
#include "texto.h"
#include "console.h"
#include "relogio.h"
#include "sistema.h"
#include "processo.h"

typedef struct {
    const char *nome;
    bool     (*iniciar)(void);
    bool     (*enviar)(const void *quadro, uint16_t bytes);
    uint16_t (*receber)(void *quadro, uint16_t capacidade);
    const uint8_t *(*mac)(void);
    bool     (*link)(void);
    uint16_t (*ids)(uint16_t *fabricante, uint16_t *modelo);
} placa_t;

static const placa_t drivers[] = {
    { "e1000", e1000_iniciar, e1000_enviar, e1000_receber, e1000_mac, e1000_link, e1000_ids },
    { "pcnet", pcnet_iniciar, pcnet_enviar, pcnet_receber, pcnet_mac, pcnet_link, pcnet_ids },
};

#define NDRIVERS ((int)(sizeof drivers / sizeof drivers[0]))

/* Endereço padrão da rede NAT do VirtualBox (o mesmo da user-net do QEMU). */
#define IP_NAT_LOCAL   0x0A00020Fu   /* 10.0.2.15 */
#define IP_NAT_GATEWAY 0x0A000202u   /* 10.0.2.2  */
#define IP_NAT_DNS     0x0A000203u   /* 10.0.2.3  */
#define MASCARA_NAT    0xFFFFFF00u

static rede_info_t cfg;
static const placa_t *placa;

/* ---------------------------------------------------------------- ARP */
#define ARP_ENTRADAS 8
#define ARP_VALIDADE 30000u   /* 300 s em tiques de 10 ms */

typedef struct {
    bool     valido;
    uint32_t ip;
    uint32_t quando;
    uint8_t  mac[6];
} arp_entrada_t;

static arp_entrada_t arp_cache[ARP_ENTRADAS];
static uint8_t arp_quadro[PCT_QUADRO_MAX];

static void arp_guardar(uint32_t ip, const uint8_t *mac)
{
    int livre = -1;
    for (int i = 0; i < ARP_ENTRADAS; i++) {
        if (arp_cache[i].valido && arp_cache[i].ip == ip) { livre = i; break; }
        if (!arp_cache[i].valido && livre < 0) livre = i;
    }
    if (livre < 0) livre = 0;                 /* sem espaço: reaproveita a primeira */
    arp_cache[livre].valido = true;
    arp_cache[livre].ip = ip;
    arp_cache[livre].quando = relogio_tiques();
    for (int i = 0; i < 6; i++) arp_cache[livre].mac[i] = mac[i];
}

static bool arp_procurar(uint32_t ip, uint8_t *mac)
{
    for (int i = 0; i < ARP_ENTRADAS; i++)
        if (arp_cache[i].valido && arp_cache[i].ip == ip) {
            if (relogio_tiques() - arp_cache[i].quando > ARP_VALIDADE) { arp_cache[i].valido = false; return false; }
            for (int j = 0; j < 6; j++) mac[j] = arp_cache[i].mac[j];
            return true;
        }
    return false;
}

static void arp_pedir(uint32_t ip)
{
    uint8_t p[28], bcast[6] = { 255, 255, 255, 255, 255, 255 };
    const uint8_t *m = rede_mac();
    pct_put16(p, 1); pct_put16(p + 2, PCT_ETH_IP); p[4] = 6; p[5] = 4;
    pct_put16(p + 6, 1);                     /* requisição */
    for (int i = 0; i < 6; i++) { p[8 + i] = m[i]; p[18 + i] = 0; }
    pct_put32(p + 14, cfg.ip);
    pct_put32(p + 24, ip);
    pct_ethernet_enviar(PCT_ETH_ARP, bcast, p, sizeof p);
}

/* Olha os quadros que chegam procurando respostas ARP; devolve true quando
 * achou uma resposta para "esperado" (e guarda no cache). */
static bool arp_colher(uint32_t esperado, uint8_t *mac)
{
    while (true) {
        uint16_t n = rede_receber(arp_quadro, sizeof arp_quadro);
        if (!n) return false;
        if (n < PCT_ETH_LEN + 28) continue;
        if (pct_get16(arp_quadro + 12) != PCT_ETH_ARP) continue;
        const uint8_t *a = arp_quadro + PCT_ETH_LEN;
        if (pct_get16(a) != 1) continue;                 /* ARP para IPv4 */
        if (pct_get16(a + 2) != PCT_ETH_IP) continue;
        uint16_t operacao = pct_get16(a + 6);
        const uint8_t *remetente_mac = a + 8;            /* endereço de hardware */
        uint32_t remetente_ip = pct_get32(a + 14);       /* endereço de protocolo */
        if (remetente_ip) arp_guardar(remetente_ip, remetente_mac);
        if (operacao == 2 && esperado && remetente_ip == esperado) {
            for (int i = 0; i < 6; i++) mac[i] = remetente_mac[i];
            return true;
        }
    }
}

bool rede_resolver(uint32_t ip, uint8_t *mac)
{
    if (!cfg.presente || !mac) return false;
    if (ip == 0 || ip == 0xFFFFFFFFu) return false;
    if (arp_procurar(ip, mac)) return true;
    for (int tentativa = 0; tentativa < 3; tentativa++) {
        arp_pedir(ip);
        pct_prazo_t prazo;
        pct_prazo_iniciar(&prazo, 300);
        while (!pct_prazo_acabou(&prazo)) {
            if (arp_colher(ip, mac)) return true;
            pct_pausa();
        }
    }
    return false;
}

bool rede_proximo_salto(uint32_t destino, uint8_t *mac)
{
    if (destino == 0xFFFFFFFFu) {
        for (int i = 0; i < 6; i++) mac[i] = 255;
        return true;
    }
    if (cfg.ip && cfg.mascara && (destino & cfg.mascara) == (cfg.ip & cfg.mascara))
        return rede_resolver(destino, mac);
    if (!cfg.gateway) return false;
    return rede_resolver(cfg.gateway, mac);
}

/* --------------------------------------------------- interface da placa */
bool rede_disponivel(void) { return cfg.presente; }

/* Cadeado da pilha de rede (ver rede.h). O dono é o pid do processo que
 * reservou; os pids só crescem (nunca são reaproveitados), então um pid
 * morto não aponta para ninguém novo. Se o dono virou zumbi (ou a entrada
 * já foi liberada), ele foi morto no meio de uma fatia e nunca vai soltar:
 * o cadeado é tomado. */
static int pilha_dono;   /* 0 = livre */

bool rede_pilha_reservar(void)
{
    if (pilha_dono == atual->pid) return true;       /* já é o dono desta fatia */
    if (pilha_dono) {
        processo_t *dono = processo_por_pid(pilha_dono);
        if (dono && dono->estado != P_ZUMBI) return false;
    }
    pilha_dono = atual->pid;
    return true;
}

void rede_pilha_liberar(void)
{
    if (pilha_dono == atual->pid) pilha_dono = 0;
}


/* Relê o link na placa: no VirtualBox ele só sobe uns segundos depois do
 * reset, então guardar o valor lido na inicialização dava "sem sinal" para
 * sempre. O comando `rede` chama isto para mostrar o estado de agora. */
bool rede_link_atual(void)
{
    if (!cfg.presente || !placa || !placa->link) return false;
    cfg.link = placa->link();
    return cfg.link;
}

const rede_info_t *rede_info(void) { return &cfg; }
const uint8_t *rede_mac(void) { return cfg.mac; }

bool rede_enviar(const void *quadro, uint16_t bytes)
{
    if (!cfg.presente || !placa) return false;
    bool ok = placa->enviar(quadro, bytes);
    if (ok) cfg.enviados++; else cfg.descartados++;
    return ok;
}

uint16_t rede_receber(void *quadro, uint16_t capacidade)
{
    if (!cfg.presente || !placa) return 0;
    uint16_t n = placa->receber(quadro, capacidade);
    if (n) cfg.recebidos++;
    return n;
}

bool rede_iniciar(void)
{
    memset(&cfg, 0, sizeof cfg);
    memset(arp_cache, 0, sizeof arp_cache);
    for (int i = 0; i < NDRIVERS; i++) {
        if (drivers[i].iniciar()) {
            placa = &drivers[i];
            break;
        }
    }
    if (!placa) return false;

    cfg.presente = true;
    const char *nome = placa->nome;
    for (int i = 0; i < REDE_NOME_MAX - 1 && nome[i]; i++) cfg.driver[i] = nome[i];
    const uint8_t *m = placa->mac();
    for (int i = 0; i < 6; i++) cfg.mac[i] = m[i];
    cfg.link = placa->link ? placa->link() : true;
    placa->ids(&cfg.id_fabricante, &cfg.id_modelo);
    /* Os drivers já esperam o link subir (no VirtualBox ele demora ~5 s);
     * aqui só se relê o estado no fim. */

    uint32_t ip = 0, mascara = 0, gateway = 0, dns = 0, servidor = 0;
    cfg.dhcp = dhcp_negociar(&ip, &mascara, &gateway, &dns, &servidor);
    if (cfg.dhcp) {
        cfg.ip = ip; cfg.mascara = mascara; cfg.gateway = gateway;
        cfg.dns = dns; cfg.dhcp_servidor = servidor;
    } else {
        cfg.ip = IP_NAT_LOCAL; cfg.mascara = MASCARA_NAT;
        cfg.gateway = IP_NAT_GATEWAY; cfg.dns = IP_NAT_DNS;
    }
    return true;
}

/* SIS_REDE_INFO: copia o relatório da rede para o programa do anel 3. */
int32_t sis_rede_info(uint32_t destino, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    if (!usuario_ok((void *)destino, sizeof(info_rede_t), true)) return ERRO_ENDERECO;
    info_rede_t *r = (info_rede_t *)destino;
    rede_link_atual();
    memset(r, 0, sizeof *r);
    r->presente = cfg.presente ? 1u : 0u;
    r->link = cfg.link ? 1u : 0u;
    r->dhcp = cfg.dhcp ? 1u : 0u;
    for (int i = 0; i < 6; i++) r->mac[i] = cfg.mac[i];
    for (int i = 0; i < REDE_NOME_MAX && cfg.driver[i]; i++) r->driver[i] = cfg.driver[i];
    r->fabricante = cfg.id_fabricante; r->modelo = cfg.id_modelo;
    r->ip = cfg.ip; r->mascara = cfg.mascara; r->gateway = cfg.gateway;
    r->dns = cfg.dns; r->servidor = cfg.dhcp_servidor;
    r->enviados = cfg.enviados; r->recebidos = cfg.recebidos;
    r->descartados = cfg.descartados;
    return cfg.presente ? 1 : 0;
}
