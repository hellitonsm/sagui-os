/* wget.c - cliente HTTP de texto, suporte inicial a http:// sem TLS. */
#include "sagui.h"

int principal(int argc,char **argv)
{
    if (argc < 2) { imprimir("Uso: wget http://host/caminho [arquivo]\n"); return 2; }
    char *r = (char *)aloca(65536);
    if (!r) { imprimir("wget: sem memoria\n"); return 1; }
    int n = http_obter(argv[1], r, 65536);
    if (n < 0) { imprimir("wget: falha de rede/HTTP (%d). Requer QEMU e1000 + NAT; somente HTTP.\n", n); libera(r); return 1; }
    int sep = -1;
    for (int i=0; i+3<n; i++)
        if (r[i]=='\r' && r[i+1]=='\n' && r[i+2]=='\r' && r[i+3]=='\n') { sep=i+4; break; }
    if (sep < 0) { imprimir("wget: resposta HTTP sem cabecalho valido (%d bytes).\n", n); libera(r); return 1; }
    escrever_tudo(SAIDA,r,(unsigned)sep-2);
    escrever_texto(SAIDA,"\n");
    int body=n-sep;
    if (argc >= 3) {
        int fd=abrir(argv[2],ABRIR_ESCREVER|ABRIR_CRIAR|ABRIR_TRUNCAR);
        if(fd<0){imprimir("wget: nao abriu %s (%d)\n",argv[2],fd);libera(r);return 1;}
        int w=escrever_tudo(fd,r+sep,(unsigned)body); fechar(fd);
        if(w<0){imprimir("wget: erro gravando arquivo (%d)\n",w);libera(r);return 1;}
        imprimir("Salvo %d bytes em %s\n",body,argv[2]);
    } else {
        escrever_tudo(SAIDA,r+sep,(unsigned)body);
        if(body && r[n-1]!='\n') escrever_texto(SAIDA,"\n");
    }
    libera(r); return 0;
}
