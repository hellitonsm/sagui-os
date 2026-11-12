/* servidor.c - liga o servidor de arquivos pela rede (H48).
 *
 * Cada fatia de servidor_arquivos() espera meio segundo por uma conexão e
 * atende quem ligar até o fim; entre as fatias este programa confere as
 * teclas de saída (q, Esc ou Ctrl+C). O cliente é o sagui-fs.ps1 do
 * Windows (tools/windows), que chega pelo encaminhamento de porta da NAT
 * do VirtualBox ou pelo hostfwd do QEMU. Rode em segundo plano com & e
 * pare com "kill <pid>", ou na frente e pare com q. */
#include "sagui.h"

/* Tecla de saída? No Terminal em janela as teclas chegam pelo pipe da
 * entrada (disponivel + ler); no console, pelo modo bruto (tecla_tentada).
 * O disponivel só funciona em pipe, então ele diz qual é o caso. O Esc vem
 * sempre em dois bytes (27 + letra das teclas especiais): só o de verdade
 * (27, 27) manda sair; o das setas e o do redesenho (27 + R, que outro
 * programa escreveu na tela e o kernel pede redesenho) são ignorados, como
 * o sagsh faz — sem isso, o eco da serial do Terminal matava o servidor. */
static int esc;   /* já veio um 27, esperando o segundo byte */

static int e_saida(unsigned char c)
{
    if (esc) {
        esc = 0;
        return c == 27;                       /* 27, 27: Esc de verdade */
    }
    if (c == 'q' || c == 'Q' || c == 3) return 1;
    if (c == 27) { esc = 1; return 0; }
    return 0;
}

static int quer_sair(void)
{
    int n = disponivel(ENTRADA);
    if (n != ERRO_INVALIDO) {                 /* Terminal em janela: pipe */
        unsigned char c;
        while (n-- > 0 && ler(ENTRADA, &c, 1) == 1)
            if (e_saida(c)) return 1;
        return 0;
    }
    for (;;) {                                 /* console: modo bruto */
        int c = tecla_tentada();
        if (c < 0) return 0;
        if (e_saida((unsigned char)c)) return 1;
    }
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    info_rede_t r;
    if (info_rede(&r) != 1 || !r.presente) {
        imprimir("servidor: sem placa de rede (veja o comando \"rede\")\n");
        return 1;
    }
    imprimir("servidor: arquivos do SaguiOS na porta 2500\n");
    imprimir("servidor: cliente no Windows: tools/windows/sagui-fs.ps1\n");
    imprimir("servidor: saia com q (ou Esc); com &, pare com kill <pid>\n");

    /* O sagsh devolve o terminal ao modo linha antes de rodar um programa;
     * o modo bruto é o que faz tecla_tentada() funcionar no console. */
    modo_terminal(TERM_BRUTO);

    int avisado = 0;
    unsigned atendidas = 0;
    for (;;) {
        if (quer_sair()) break;
        int n = servidor_arquivos(500);
        if (n == 1) {
            atendidas++;
            imprimir("servidor: conexao %u atendida\n", atendidas);
        } else if (n == ERRO_OCUPADO) {
            if (!avisado) {
                imprimir("servidor: a rede esta ocupada (wget?); tentando de novo\n");
                avisado = 1;
            }
        } else if (n < 0) {
            imprimir("servidor: erro %d\n", n);
            break;
        }
    }
    modo_terminal(TERM_LINHA);
    imprimir("servidor: encerrado (%u conexoes)\n", atendidas);
    return 0;
}
