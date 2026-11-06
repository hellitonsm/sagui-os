/* Testa o tocador (.mus/.vid) no proprio SaguiOS. Uma feature por caso. */
#include "sagui.h"

static int rodar(char *const args[], char *buf, unsigned cap)
{
    int f[2], codigo = -1;
    if (criar_pipe(f) < 0) return -1;
    int a = duplicar(SAIDA, 14), b = duplicar(ERROS, 15);
    if (a < 0 || b < 0) {
        if (a >= 0) fechar(a);
        if (b >= 0) fechar(b);
        fechar(f[0]); fechar(f[1]); return -1;
    }
    duplicar(f[1], SAIDA); duplicar(f[1], ERROS);
    int pid = executar(args[0], args);
    duplicar(a, SAIDA); duplicar(b, ERROS); fechar(a); fechar(b); fechar(f[1]);
    if (pid < 0) { fechar(f[0]); return -1; }
    if (esperar(pid, &codigo) < 0) { fechar(f[0]); return -1; }
    int n = ler(f[0], buf, cap - 1); fechar(f[0]);
    if (n < 0) return -1;
    buf[n] = 0;
    return codigo;
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    char b[1024];
    /* 1. info de uma musica .mus */
    char *info_mus[] = {"/bin/tocador", "--info", "/musicas/demo.mus", NULL};
    if (rodar(info_mus, b, sizeof b) != 0) return 1;
    if (compara_texto(b, "tocador: musica \"Demo SaguiOS\" 5 notas\n")) return 2;
    /* 2. tocar a musica .mus */
    char *toca_mus[] = {"/bin/tocador", "/musicas/demo.mus", NULL};
    if (rodar(toca_mus, b, sizeof b) != 0) return 3;
    if (!acha_char(b, 't')) return 4;
    /* 3. info de um video .vid */
    char *info_vid[] = {"/bin/tocador", "--info", "/videos/demo.vid", NULL};
    if (rodar(info_vid, b, sizeof b) != 0) return 5;
    if (compara_texto(b, "tocador: video /videos/demo.vid 2 quadros\n")) return 6;
    /* 4. .mp3 recusado */
    char *mp3[] = {"/bin/tocador", "/musicas/x.mp3", NULL};
    b[0] = 0;
    if (rodar(mp3, b, sizeof b) != 1) return 7;
    if (compara_texto(b, "tocador: formato nao suportado (/musicas/x.mp3)\ntocador: use .mus/.vid (sem codec/DAC no hardware)\n")) return 8;
    /* 5. arquivo .mus inexistente */
    char *falta[] = {"/bin/tocador", "/musicas/nao-existe.mus", NULL};
    if (rodar(falta, b, sizeof b) != 1) return 9;
    registrar("[TOC] musica e video validados\n");
    return 0;
}
