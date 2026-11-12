/* expressao.h - expressões regulares para os programas do SaguiOS (H50).
 *
 * Um motor pequeno e próprio, sem alocação e sem libc: o padrão é analisado
 * (sintaxe BRE ou ERE, como o grep do Linux), vira uma árvore e depois um
 * autômato finito (NFA) que o grep percorre uma vez por linha.  O mesmo motor
 * atende a existência ("casa?") e o trecho encontrado ("buscar"), usado pelo
 * -o e pelo --color.
 *
 * O texto é Latin-1 (um byte por caractere), que é o que o SaguiOS usa nas
 * telas e nos arquivos depois da conversão de UTF-8 (D027).  As classes
 * POSIX e a dobra de maiúsculas/minúsculas conhecem as letras do Latin-1.
 *
 * Limites (constantes, para caber sem alocação):
 *   EXP_MAX_NOS      nós da árvore (padrão grande demais -> erro)
 *   EXP_MAX_ESTADOS  instruções do NFA (repetições grandes -> erro)
 *   EXP_MAX_CLASSES  classes [ ... ] distintas por padrão
 *   EXP_MAX_PILHA    pilha da simulação (igual a EXP_MAX_ESTADOS)
 */
#ifndef SAGUI_EXPRESSAO_H
#define SAGUI_EXPRESSAO_H

#define EXP_MAX_NOS      512
#define EXP_MAX_ESTADOS  768
#define EXP_MAX_CLASSES  32
#define EXP_MAX_PILHA    2048

/* Opções de expressao_compilar(). */
#define EXP_CASO_INSENSIVEL 1   /* -i: letras maiúsculas e minúsculas são iguais */
#define EXP_ESTENDIDA       2   /* -E: sintaxe ERE (senão, BRE como o grep padrão) */
#define EXP_LINHA_INTEIRA   4   /* -x: o padrão tem de casar a linha toda */
#define EXP_PALAVRA_INTEIRA 8   /* -w: o trecho tem de ser uma palavra inteira */

/* Tipos internos.  Ficam no cabeçalho porque o chamador aloca a estrutura
 * (o motor não chama aloca/libera). */
enum {
    EXP_NO_VAZIO = 0,
    EXP_NO_CAR,
    EXP_NO_CAR_CI,
    EXP_NO_PONTO,
    EXP_NO_CLASSE,
    EXP_NO_CONCAT,
    EXP_NO_ALT,
    EXP_NO_REPETIR,
    EXP_NO_BOL,          /* ^ */
    EXP_NO_EOL,          /* $ */
    EXP_NO_PALAVRA,      /* \b */
    EXP_NO_NAO_PALAVRA,  /* \B */
    EXP_NO_INI_PALAVRA,  /* \< */
    EXP_NO_FIM_PALAVRA,  /* \> */
    EXP_NO_INI_OK,       /* -w: começo do trecho é fronteira */
    EXP_NO_FIM_OK        /* -w: fim do trecho é fronteira */
};

typedef struct {
    unsigned char tipo;
    unsigned char classe;    /* EXP_NO_CAR: o byte; EXP_NO_CLASSE: índice */
    short esq, dir;          /* filhos (-1 = nenhum) */
    short min, max;          /* EXP_NO_REPETIR: máximo -1 = sem limite */
} exp_no_t;

typedef struct {
    unsigned char bits[32];  /* 256 bits, um por byte Latin-1 */
} exp_classe_t;

/* Instruções do NFA. */
enum {
    EXP_I_CAR = 0,        /* consome um byte igual a "classe" */
    EXP_I_CAR_CI,         /* consome um byte, ignorando maiúsculas */
    EXP_I_CLASSE,         /* consome um byte da classe "classe" */
    EXP_I_PONTO,          /* consome qualquer byte menos '\n' */
    EXP_I_DIVIDE,         /* epsilon: vai para saida1 ou saida2 */
    EXP_I_NOP,            /* epsilon: vai para saida1 */
    EXP_I_BOL,            /* epsilon: só no começo do texto */
    EXP_I_EOL,            /* epsilon: só no fim do texto */
    EXP_I_PALAVRA,        /* epsilon: fronteira de palavra */
    EXP_I_NAO_PALAVRA,    /* epsilon: não é fronteira de palavra */
    EXP_I_INI_PALAVRA,    /* epsilon: começo de palavra */
    EXP_I_FIM_PALAVRA,    /* epsilon: fim de palavra */
    EXP_I_INI_OK,         /* -w: byte antes do começo do trecho não é de palavra */
    EXP_I_FIM_OK,         /* -w: byte depois do fim do trecho não é de palavra */
    EXP_I_FIM             /* aceita */
};

typedef struct {
    unsigned char tipo;
    unsigned char classe;    /* byte (EXP_I_CAR*) ou índice da classe */
    short saida1, saida2;
} exp_inst_t;

typedef struct {
    exp_no_t     nos[EXP_MAX_NOS];
    int          qtd_nos;
    exp_classe_t classes[EXP_MAX_CLASSES];
    int          qtd_classes;
    exp_inst_t   inst[EXP_MAX_ESTADOS];
    int          qtd_inst;
    int          inicio;      /* estado inicial do NFA */
    int          final;       /* estado de aceitação */
    const char  *erro;        /* NULL = compilou; senão, mensagem estática */
    int          erro_pos;    /* posição no padrão (0 = começo) */
} expressao_t;

/* Compila "padrao" (terminado em zero) com as opções EXP_*.  Devolve 0 em
 * caso de erro (a mensagem fica em expressao_erro()). */
int expressao_compilar(expressao_t *e, const char *padrao, int opcoes);

/* 1 se o padrão casa em algum ponto do texto; 0 se não. */
int expressao_casa(const expressao_t *e, const char *texto, int n);

/* Procura o primeiro trecho que casa a partir de "inicio" (0 = começo).
 * Devolve 1 e preenche ini e fim (fim exclusivo) com o trecho mais à
 * esquerda e, entre os que começam no mesmo ponto, o mais longo. */
int expressao_buscar(const expressao_t *e, const char *texto, int n, int inicio,
                     int *ini, int *fim);

/* Mensagem do último erro de compilação (NULL se não houve). */
const char *expressao_erro(const expressao_t *e);

#endif
