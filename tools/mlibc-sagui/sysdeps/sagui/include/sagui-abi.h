/* sagui-abi.h - subconjunto ASCII de kernel/inc/abi.h usado pelo porte.
 *
 * O abi.h do kernel e escrito em Latin-1; aqui repetimos apenas as constantes
 * e os layouts que o sysdeps precisa, com os mesmos valores e tamanhos. */
#ifndef SAGUI_MLIBC_ABI_H
#define SAGUI_MLIBC_ABI_H

#define SIS_EXECUTAR        1
#define SIS_ESPERAR         2
#define SIS_SAIR            3
#define SIS_PID             4
#define SIS_MATAR           5
#define SIS_ABRIR          10
#define SIS_LER            11
#define SIS_ESCREVER       12
#define SIS_FECHAR         13
#define SIS_POSICIONAR     14
#define SIS_INFO_ARQUIVO   15
#define SIS_CRIAR_PASTA    20
#define SIS_REMOVER_PASTA  21
#define SIS_APAGAR         22
#define SIS_LISTAR         23
#define SIS_MUDAR_PASTA    24
#define SIS_PASTA_ATUAL    25
#define SIS_RENOMEAR       26
#define SIS_PIPE           30
#define SIS_DUPLICAR       31
#define SIS_DISPONIVEL     32
#define SIS_DORMIR         40
#define SIS_TEMPO          41
#define SIS_DATA           42
#define SIS_INFO_SISTEMA   43
#define SIS_AUMENTAR_MEM   44
#define SIS_REGISTRAR      48
#define SIS_TLS            62

#define ERRO_INVALIDO      -1
#define ERRO_NAO_EXISTE    -2
#define ERRO_JA_EXISTE     -3
#define ERRO_SEM_MEMORIA   -4
#define ERRO_ENDERECO      -5
#define ERRO_NAO_E_PASTA   -6
#define ERRO_E_PASTA       -7
#define ERRO_NAO_VAZIA     -8
#define ERRO_LIMITE        -9
#define ERRO_DESCRITOR    -10
#define ERRO_PIPE_QUEBRADO -11
#define ERRO_SEM_PROCESSO -12
#define ERRO_FORMATO      -13
#define ERRO_ACESSO       -14
#define ERRO_OCUPADO      -15

#define ABRIR_LER          1
#define ABRIR_ESCREVER     2
#define ABRIR_CRIAR        4
#define ABRIR_TRUNCAR      8
#define ABRIR_ACRESCENTAR 16

#define POS_INICIO 0
#define POS_ATUAL  1
#define POS_FIM    2

#define TIPO_ARQUIVO  1
#define TIPO_PASTA    2
#define TIPO_PROGRAMA 3
#define TIPO_TERMINAL 4
#define TIPO_PIPE     5

#define NOME_MAX 64

#define INFO_MEMORIA   1
#define INFO_PROCESSOS 2
#define INFO_VERSAO    3

typedef struct {
    unsigned int tipo;
    unsigned int tamanho;
} sg_info_arquivo_t;

typedef struct {
    char         nome[NOME_MAX];
    unsigned int tipo;
    unsigned int tamanho;
} sg_entrada_pasta_t;

typedef struct {
    unsigned short ano;
    unsigned char  mes, dia, hora, minuto, segundo, dia_semana;
} sg_data_t;

typedef struct {
    int          pid;
    int          pai;
    unsigned int estado;
    unsigned int memoria_kb;
    unsigned int cpu_ms;
    unsigned int usuario;
    char         nome[32];
} sg_info_processo_t;

#define SAIDA_ENCERRADO_PELO_KERNEL -1000

#endif /* SAGUI_MLIBC_ABI_H */
