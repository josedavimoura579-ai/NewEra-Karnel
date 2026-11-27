/* nlibc - signal.h: sinais, como no POSIX. Os números são os do Linux.
 * O núcleo entrega o sinal chamando o tratador na pilha do programa; quando
 * ele volta, o __sinal_volta (usuario/sys.s) devolve tudo como estava. */
#ifndef _SIGNAL_H
#define _SIGNAL_H
#include <sys/types.h>

#define SIGHUP     1
#define SIGINT     2
#define SIGQUIT    3
#define SIGILL     4
#define SIGTRAP    5
#define SIGABRT    6
#define SIGIOT     SIGABRT
#define SIGBUS     7
#define SIGFPE     8
#define SIGKILL    9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGSTKFLT 16
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22
#define SIGURG    23
#define SIGXCPU   24
#define SIGXFSZ   25
#define SIGVTALRM 26
#define SIGPROF   27
#define SIGWINCH  28
#define SIGIO     29
#define SIGPOLL   SIGIO
#define SIGPWR    30
#define SIGSYS    31
#define NSIG      32
#define _NSIG     NSIG

typedef int sig_atomic_t;
typedef unsigned long sigset_t;
typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

typedef struct {
    int   si_signo;
    int   si_errno;
    int   si_code;
    int   __pad;
    void *si_addr;           /* SIGSEGV, SIGFPE...: o endereço da falha */
    pid_t si_pid;
    int   si_status;
    int   __resto[24];
} siginfo_t;

struct sigaction {
    void   (*sa_handler)(int);
    void   (*sa_sigaction)(int, siginfo_t *, void *);   /* com SA_SIGINFO */
    sigset_t sa_mask;
    int      sa_flags;
    void   (*sa_restorer)(void);
};

#define SA_NOCLDSTOP 1
#define SA_SIGINFO   4
#define SA_ONSTACK   0x08000000
#define SA_RESTART   0x10000000     /* (é sempre assim no NovaEra) */
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

sighandler_t signal(int sinal, sighandler_t f);
int  sigaction(int sinal, const struct sigaction *nova, struct sigaction *antiga);
int  kill(pid_t pid, int sinal);
int  killpg(pid_t grupo, int sinal);
int  raise(int sinal);
int  sigprocmask(int como, const sigset_t *nova, sigset_t *antiga);
int  sigemptyset(sigset_t *s);
int  sigfillset(sigset_t *s);
int  sigaddset(sigset_t *s, int sinal);
int  sigdelset(sigset_t *s, int sinal);
int  sigismember(const sigset_t *s, int sinal);
void psignal(int sinal, const char *texto);
extern const char *const sys_siglist[NSIG];
#endif
