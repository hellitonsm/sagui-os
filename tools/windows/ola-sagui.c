/* ola-sagui.c - teste de capacidade: um programa C "de verdade", com
 * interface grafica, compilado DENTRO do SaguiOS pelo tinycc, a partir de
 * um fonte enviado do Windows pela rede (sagui-fs.ps1, H48).
 *
 * No Terminal do SaguiOS, na pasta onde este fonte estiver:
 *
 *   tinycc -o ola ola-sagui.c -lsaguigui
 *   ./ola
 *
 * A caixa de mensagem espera o botao OK (ou Enter, Esc ou o X da janela).
 */
#include <stdio.h>
#include <gui.h>

int main(void)
{
    mensagem("Ola Sagui", "ola sagui, sistema testado com sucesso");
    puts("[ola-sagui] caixa de mensagem mostrada e fechada");
    return 0;
}
