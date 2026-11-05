/* rede.c - mostra a placa de rede, o endereço IP e como ele foi obtido.
 *
 * É o "ipconfig" do SaguiOS: serve para conferir, dentro do VirtualBox ou do
 * QEMU, qual placa o sistema encontrou (Intel PRO/1000 ou AMD PCnet), o MAC,
 * se o link está ligado e se o endereço veio do DHCP ou é o fixo da NAT. */
#include "sagui.h"

static void mostrar_ip(unsigned int ip)
{
    imprimir("%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
             (ip >> 8) & 0xFF, ip & 0xFF);
}

/* Máscara em "/24" etc.: conta os bits 1 seguidos. */
static int bits_da_mascara(unsigned int m)
{
    int n = 0;
    for (int i = 31; i >= 0; i--) {
        if (m & (1u << i)) n++;
        else break;
    }
    return n;
}

int principal(int argc, char **argv)
{
    info_rede_t r;
    (void)argc; (void)argv;
    if (info_rede(&r) < 0) { imprimir("rede: o kernel recusou o pedido\n"); return 1; }
    if (!r.presente) {
        imprimir("rede: nenhuma placa conhecida (Intel 8254x ou AMD PCnet).\n");
        imprimir("      No VirtualBox use Adaptador 1 = \"Intel PRO/1000 MT Desktop\"\n");
        imprimir("      ou \"PCnet-FAST III\", ligado a NAT.\n");
        return 1;
    }

    imprimir("Placa....: %s (%x:%x)\n", r.driver, r.fabricante, r.modelo);
    imprimir("MAC......: %x:%x:%x:%x:%x:%x\n", r.mac[0], r.mac[1], r.mac[2],
             r.mac[3], r.mac[4], r.mac[5]);
    imprimir("Link.....: %s\n", r.link ? "ligado" : "sem sinal");
    if (!r.link)
        imprimir("           (no VirtualBox o link sobe ~5 s depois do boot;\n"
                 "            confira Adaptador 1 ligado e com \"Cabo conectado\")\n");

    imprimir("Endereco.: ");
    mostrar_ip(r.ip);
    imprimir("/%d (%s)\n", bits_da_mascara(r.mascara),
             r.dhcp ? "DHCP" : "fixo da NAT");
    imprimir("Mascara..: ");
    mostrar_ip(r.mascara);
    imprimir("\nGateway..: ");
    mostrar_ip(r.gateway);
    imprimir("\nDNS......: ");
    mostrar_ip(r.dns);
    imprimir("\n");
    if (r.dhcp) {
        imprimir("Servidor.: ");
        mostrar_ip(r.servidor);
        imprimir(" (DHCP)\n");
    }
    imprimir("Quadros..: %u enviados, %u recebidos\n", r.enviados, r.recebidos);
    if (r.descartados) imprimir("           %u descartados\n", r.descartados);
    imprimir("Teste....: wget http://10.0.2.2:8000/arquivo (NAT, com um servidor\n"
             "           HTTP rodando no anfitriao) ou http://<ip da rede>/\n");
    return 0;
}
