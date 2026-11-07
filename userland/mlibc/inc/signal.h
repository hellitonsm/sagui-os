/* signal.h - o SaguiOS nao tem sinais; estes stubs existem porque o
 * tinycc instala um tratador para diagnosticar falhas de tempo de
 * execucao (ja que nem isso roda aqui). */
#ifndef SAGUI_MLIBC_SIGNAL_H
#define SAGUI_MLIBC_SIGNAL_H

typedef int sig_atomic_t;

typedef struct {
    unsigned long __bits[8];
} sigset_t;

#define SIG_UNBLOCK   1
#define SIG_BLOCK     0
#define SIG_SETMASK  2

#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGBUS    7
#define SIGFPE    8
#define SIGINT    2
#define SIGTERM   15
#define SIGSEGV   11

#define FPE_INTDIV  1
#define FPE_INTOVF  2
#define FPE_FLTDIV  3
#define FPE_FLTOVF  4
#define FPE_FLTUND  5
#define FPE_FLTRES  6
#define FPE_FLTINV  7
#define FPE_FLTSUB  8

#define SA_SIGINFO  0x00000004u
#define SA_ONSTACK  0x08000000u
#define SA_RESETHAND 0x80000000u

typedef struct {
    int si_code;
    void *si_addr;
} siginfo_t;

struct sigaction {
    void (*sa_handler)(int);
    void (*sa_sigaction)(int, siginfo_t *, void *);
    sigset_t sa_mask;
    int sa_flags;
};

static inline int sigemptyset(sigset_t *conjunto)
{
    if (conjunto) {
        for (unsigned i = 0; i < sizeof conjunto->__bits / sizeof conjunto->__bits[0]; i++)
            conjunto->__bits[i] = 0;
    }
    return 0;
}

static inline int sigaddset(sigset_t *conjunto, int sinal)
{
    (void)sinal;
    (void)conjunto;
    return 0;
}

static inline int sigprocmask(int como, const sigset_t *conjunto, sigset_t *anterior)
{
    (void)como; (void)conjunto; (void)anterior;
    return 0;
}

static inline int sigaction(int sinal, const struct sigaction *acao, struct sigaction *antiga)
{
    (void)sinal; (void)acao; (void)antiga;
    return 0;
}

#endif
