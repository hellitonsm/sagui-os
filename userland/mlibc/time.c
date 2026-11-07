/* time.c - CLOCK_MONOTONIC vem do PIT; CLOCK_REALTIME usa o CMOS como UTC. */
#include "internal.h"
#include <time.h>
#include <sys/time.h>
#include <limits.h>

static int bissexto(int ano)
{
    return ano % 4 == 0 && (ano % 100 != 0 || ano % 400 == 0);
}

static int dias_no_mes(int ano, int mes)
{
    static const unsigned char dias[] = { 31, 28, 31, 30, 31, 30,
                                          31, 31, 30, 31, 30, 31 };
    if (mes < 1 || mes > 12) return 0;
    return dias[mes - 1] + (mes == 2 && bissexto(ano));
}

clock_t clock(void)
{
    /* INFO_PROCESSOS expos o tempo efetivo de CPU em milissegundos. */
    info_processo_t processos[64];
    int n = info_sistema(INFO_PROCESSOS, processos, 64);
    if (n < 0) { __mlibc_resultado(n); return (clock_t)-1; }
    int pid = meu_pid();
    for (int i = 0; i < n; i++) {
        if (processos[i].pid != pid) continue;
        if (processos[i].cpu_ms > (unsigned long)LONG_MAX) {
            errno = EOVERFLOW;
            return (clock_t)-1;
        }
        return (clock_t)processos[i].cpu_ms;
    }
    errno = ESRCH;
    return (clock_t)-1;
}

int clock_gettime(clockid_t clock_id, struct timespec *result)
{
    if (!result) { errno = EFAULT; return -1; }
    if (clock_id == CLOCK_MONOTONIC) {
        unsigned ms = tempo_ms();
        result->tv_sec = (time_t)(ms / 1000u);
        result->tv_nsec = (long)(ms % 1000u) * 1000000L;
        return 0;
    }
    if (clock_id != CLOCK_REALTIME) { errno = EINVAL; return -1; }

    data_t d;
    int r = ler_data(&d);
    if (r < 0) return __mlibc_resultado(r);
    int ano = d.ano;
    if (ano < 1970 || ano > 2038 || d.dia < 1 || d.dia > dias_no_mes(ano, d.mes) ||
        d.hora > 23 || d.minuto > 59 || d.segundo > 59) {
        errno = EOVERFLOW;
        return -1;
    }
    unsigned dias = 0;
    for (int y = 1970; y < ano; y++) dias += bissexto(y) ? 366u : 365u;
    for (int m = 1; m < d.mes; m++) dias += (unsigned)dias_no_mes(ano, m);
    dias += (unsigned)d.dia - 1u;
    unsigned long segundos = (unsigned long)dias * 86400UL +
                             (unsigned long)d.hora * 3600UL +
                             (unsigned long)d.minuto * 60UL + d.segundo;
    if (segundos > (unsigned long)LONG_MAX) { errno = EOVERFLOW; return -1; }
    result->tv_sec = (time_t)segundos;
    result->tv_nsec = 0; /* CMOS tem resolucao de um segundo. */
    return 0;
}

time_t time(time_t *result)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0) return (time_t)-1;
    if (result) *result = ts.tv_sec;
    return ts.tv_sec;
}

int nanosleep(const struct timespec *request, struct timespec *remaining)
{
    if (!request) { errno = EFAULT; return -1; }
    if (request->tv_sec < 0 || request->tv_nsec < 0 || request->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }
    long seconds = request->tv_sec;
    unsigned extra_ms = (unsigned)((request->tv_nsec + 999999L) / 1000000L);
    /* SIS_DORMIR calcula ms*100 em 32 bits; 40.000.000 ms ainda e seguro. */
    while (seconds >= 40000) {
        int r = dormir(40000000u);
        if (r < 0) return __mlibc_resultado(r);
        seconds -= 40000;
    }
    unsigned ms = (unsigned)seconds * 1000u + extra_ms;
    if (ms) {
        int r = dormir(ms);
        if (r < 0) return __mlibc_resultado(r);
    }
    if (remaining) { remaining->tv_sec = 0; remaining->tv_nsec = 0; }
    return 0;
}

unsigned int sleep(unsigned int seconds)
{
    struct timespec request = { (time_t)seconds, 0 };
    return nanosleep(&request, NULL) < 0 ? seconds : 0;
}

int usleep(unsigned int microseconds)
{
    if (microseconds >= 1000000u) { errno = EINVAL; return -1; }
    struct timespec request = { 0, (long)microseconds * 1000L };
    return nanosleep(&request, NULL);
}

int gettimeofday(struct timeval *value, struct timezone *zone)
{
    if (!value) { errno = EFAULT; return -1; }
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0) return -1;
    value->tv_sec = ts.tv_sec;
    value->tv_usec = ts.tv_nsec / 1000L;
    if (zone) { zone->tz_minuteswest = 0; zone->tz_dsttime = 0; }
    return 0;
}

/* Tempo (UTC) -> struct tm. O kernel nao tem fuso; o SaguiOS trata
 * tudo como UTC. dias_1970 e o n. de dias desde 1970-01-01 (quinta-feira). */
struct tm *localtime(const time_t *tempo)
{
    static struct tm resultado;
    if (!tempo) { errno = EFAULT; return (struct tm *)0; }
    long segundos = (long)*tempo;
    long dias = segundos / 86400;
    long resto = segundos % 86400;
    if (resto < 0) { resto += 86400; dias -= 1; }
    resultado.tm_hour = (int)(resto / 3600);
    resultado.tm_min  = (int)((resto % 3600) / 60);
    resultado.tm_sec  = (int)(resto % 60);
    resultado.tm_wday = (int)(((dias % 7) + 4 + 7) % 7);
    resultado.tm_isdst = 0;
    int ano = 1970;
    if (dias >= 0) {
        for (;;) {
            long d = bissexto(ano) ? 366 : 365;
            if (dias < d) break;
            dias -= d;
            ano++;
        }
    } else {
        for (;;) {
            int d = bissexto(ano - 1) ? 366 : 365;
            if (dias >= -(long)d) { ano--; dias += d; break; }
            ano--;
            dias += d;
        }
    }
    int yday = (int)dias;
    int mes = 1;
    int restante = yday;
    while (mes < 12 && restante >= dias_no_mes(ano, mes)) {
        restante -= dias_no_mes(ano, mes);
        mes++;
    }
    resultado.tm_year = ano - 1900;
    resultado.tm_mon  = mes - 1;
    resultado.tm_mday = restante + 1;
    resultado.tm_yday = yday;
    return &resultado;
}
