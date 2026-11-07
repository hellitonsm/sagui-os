/* sys/ucontext.h - contexto da CPU no estilo do Linux i386; o SaguiOS
 * nao tem sinais, mas o tinycc le o contexto so no diagnostico de falhas. */
#ifndef SAGUI_MLIBC_SYS_UCONTEXT_H
#define SAGUI_MLIBC_SYS_UCONTEXT_H

typedef unsigned long greg_t;
#define NGREG 19

typedef struct {
    greg_t gregs[NGREG];
} mcontext_t;

typedef struct ucontext {
    unsigned long uc_flags;
    struct ucontext *uc_link;
    mcontext_t uc_mcontext;
    unsigned long uc_stack[3];
    unsigned long uc_filler[128];
} ucontext_t;

/* Indices dos registradores em mcontext_t.gregs (ABI i386 do Linux). */
#define REG_GS     0
#define REG_FS     1
#define REG_ES     2
#define REG_DS     3
#define REG_EDI    4
#define REG_ESI    5
#define REG_EBP    6
#define REG_ESP    7
#define REG_EBX    8
#define REG_EDX    9
#define REG_ECX    10
#define REG_EAX    11
#define REG_EIP    14
#define REG_CS     15
#define REG_EFL    16
#define REG_UESP   17
#define REG_SS     18

#endif
