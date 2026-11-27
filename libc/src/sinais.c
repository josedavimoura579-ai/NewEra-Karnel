/*
 * nlibc - libc/src/sinais.c
 * signal, sigaction, kill, raise, sigprocmask, grupos de processos, sleep.
 *
 * O núcleo guarda, para cada sinal, um endereço de tratador. A libc põe lá
 * sempre a mesma função, __tratador: ela conta o sinal (é como o pause() e
 * o sleep() sabem que foram interrompidos), cuida do SA_RESETHAND e chama
 * a função que o programa pediu. Quando ela volta, cai no __sinal_volta
 * (usuario/sys.s), e o núcleo devolve o programa ao ponto interrompido.
 */
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include "interno.h"

typedef void (*tratador_t)(int, siginfo_t *, void *);

static tratador_t tratadores[NSIG];   /* a função do programa, para cada sinal */
static int        flags[NSIG];
static unsigned   recebidos;          /* quantos sinais já foram tratados */

static void __tratador(int s, siginfo_t *info, void *ctx)
{
    recebidos++;
    if (s <= 0 || s >= NSIG) return;
    tratador_t f = tratadores[s];
    if (flags[s] & SA_RESETHAND) signal(s, SIG_DFL);
    if (f) f(s, info, ctx);
}

int sigaction(int s, const struct sigaction *nova, struct sigaction *antiga)
{
    if (s <= 0 || s >= NSIG) { errno = EINVAL; return -1; }
    sinal_acao_t a;
    a.tratador = a.restaurador = 0;
    a.mascara  = 0;
    a.mudar    = 0;
    tratador_t velho = tratadores[s];
    int velhas_flags = flags[s];
    if (nova) {
        tratador_t f = (nova->sa_flags & SA_SIGINFO) ? nova->sa_sigaction
                                                     : (tratador_t)nova->sa_handler;
        if ((sighandler_t)f == SIG_DFL || (sighandler_t)f == SIG_IGN) {
            a.tratador    = (u64)f;
            tratadores[s] = 0;
        } else {                      /* antes do núcleo: o sinal pode chegar já */
            tratadores[s] = f;
            a.tratador    = (u64)__tratador;
            a.restaurador = (u64)__sinal_volta;
        }
        flags[s]  = nova->sa_flags;
        a.mascara = (u32)nova->sa_mask;
        a.mudar   = 1;
    }
    if (sys_sinal_tratar((u32)s, &a) < 0) {
        tratadores[s] = velho;
        flags[s]      = velhas_flags;
        errno = EINVAL;
        return -1;
    }
    if (antiga) {
        /* o núcleo devolveu o que havia: padrão, ignorar ou o __tratador */
        sighandler_t h = a.tratador <= SINAL_IGNORAR ? (sighandler_t)a.tratador
                                                     : (sighandler_t)velho;
        antiga->sa_handler   = h;
        antiga->sa_sigaction = (tratador_t)h;
        antiga->sa_mask      = a.mascara;
        antiga->sa_flags     = a.tratador > SINAL_IGNORAR ? velhas_flags : 0;
        antiga->sa_restorer  = 0;
    }
    return 0;
}

sighandler_t signal(int s, sighandler_t f)
{
    struct sigaction nova, antiga;
    nova.sa_handler   = f;
    nova.sa_sigaction = 0;
    nova.sa_mask      = 0;
    nova.sa_flags     = 0;
    nova.sa_restorer  = 0;
    if (sigaction(s, &nova, &antiga) < 0) return SIG_ERR;
    return antiga.sa_handler;
}

int kill(pid_t pid, int s)
{
    if (s < 0 || s >= NSIG) { errno = EINVAL; return -1; }
    long r = sys_sinal((long)pid, (u32)s);
    if (r < 0) {
        errno = r == ERRO_SEM_PERMISSAO ? EPERM : r == ERRO_INVALIDO ? EINVAL : ESRCH;
        return -1;
    }
    return 0;
}

int killpg(pid_t grupo, int s)
{
    if (grupo <= 0) { errno = EINVAL; return -1; }
    return kill(-grupo, s);
}

/* O sinal chega na volta da chamada de sistema: quando raise() retorna, o
 * tratador já rodou (ou o processo já acabou). */
int raise(int s) { return kill((pid_t)sys_pid(), s); }

int sigprocmask(int como, const sigset_t *nova, sigset_t *antiga)
{
    long r;
    if (nova) {
        if (como != SIG_BLOCK && como != SIG_UNBLOCK && como != SIG_SETMASK) {
            errno = EINVAL;
            return -1;
        }
        r = sys_sinal_mascara((u32)como, (u32)*nova);
    } else {
        r = sys_sinal_mascara(SINAL_BLOQUEAR, 0);            /* só consulta */
    }
    if (r < 0) { errno = EINVAL; return -1; }
    if (antiga) *antiga = (sigset_t)r;
    return 0;
}

int sigemptyset(sigset_t *c) { *c = 0; return 0; }
int sigfillset(sigset_t *c)  { *c = 0xFFFFFFFEul; return 0; }

int sigaddset(sigset_t *c, int s)
{
    if (s <= 0 || s >= NSIG) { errno = EINVAL; return -1; }
    *c |= 1ul << s;
    return 0;
}

int sigdelset(sigset_t *c, int s)
{
    if (s <= 0 || s >= NSIG) { errno = EINVAL; return -1; }
    *c &= ~(1ul << s);
    return 0;
}

int sigismember(const sigset_t *c, int s)
{
    if (s <= 0 || s >= NSIG) { errno = EINVAL; return -1; }
    return (*c >> s) & 1;
}

/* ---------------------------- Grupos de processos ------------------------- */

int setpgid(pid_t pid, pid_t grupo)
{
    if (grupo < 0) { errno = EINVAL; return -1; }
    long r = sys_grupo((u32)pid, grupo);
    if (r < 0) { errno = r == ERRO_SEM_PERMISSAO ? EPERM : ESRCH; return -1; }
    return 0;
}

pid_t getpgid(pid_t pid)
{
    long r = sys_grupo((u32)pid, -1);
    if (r < 0) { errno = ESRCH; return -1; }
    return (pid_t)r;
}

pid_t getpgrp(void) { return getpgid(0); }

/* Sem sessões nem terminal de controle: só um grupo novo. */
pid_t setsid(void)
{
    if (setpgid(0, 0) < 0) return -1;
    return getpid();
}

/* --------------------------------- Esperar -------------------------------
 * O núcleo conta o tempo em ticks de 10 ms (SYS_TEMPO). Enquanto espera, o
 * processo passa a vez (SYS_CEDER). Um sinal tratado interrompe a espera. */
static int dormir_ticks(u32 ticks)
{
    unsigned antes = recebidos;
    u32 ini = sys_tempo();
    while (sys_tempo() - ini < ticks) {
        if (recebidos != antes) return (int)(ticks - (sys_tempo() - ini));
        sys_ceder();
    }
    return 0;
}

unsigned sleep(unsigned segundos)
{
    int falta = dormir_ticks(segundos * 100u);
    return (unsigned)((falta + 99) / 100);
}

int usleep(unsigned us)
{
    if (dormir_ticks((us + 9999u) / 10000u)) { errno = EINTR; return -1; }
    return 0;
}

int pause(void)
{
    unsigned antes = recebidos;
    while (recebidos == antes) sys_ceder();
    errno = EINTR;
    return -1;
}

/* Sai na hora, sem atexit nem esvaziar o stdio (o que um tratador pode chamar). */
_Noreturn void _exit(int codigo)
{
    sys_sair(codigo);
    for (;;) ;
}
