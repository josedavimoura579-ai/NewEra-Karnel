/* nlibc - signal.h: o NovaEra ainda não tem sinais; kill() devolve ENOSYS. */
#ifndef _SIGNAL_H
#define _SIGNAL_H
#include <sys/types.h>
#define SIGINT  2
#define SIGKILL 9
#define SIGTERM 15
typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
int kill(pid_t pid, int sinal);
sighandler_t signal(int sinal, sighandler_t f);
#endif
