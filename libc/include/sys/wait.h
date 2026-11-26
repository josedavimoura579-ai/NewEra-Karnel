/* nlibc - sys/wait.h
 * O status guarda o código de saída nos bits 8-15, como no Unix. Um
 * processo encerrado por uma exceção sai com 128 + o vetor da CPU. */
#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H
#include <sys/types.h>
#define WNOHANG 1
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WIFEXITED(s)   (((s) & 0x7f) == 0)
#define WIFSIGNALED(s) 0
#define WTERMSIG(s)    0
#define WIFSTOPPED(s)  0
pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int opcoes);
#endif
