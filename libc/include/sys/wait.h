/* nlibc - sys/wait.h
 * O status é o do Unix: o código de saída nos bits 8-15, ou o sinal que
 * encerrou o processo nos bits 0-6. */
#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H
#include <sys/types.h>
#define WNOHANG   1
#define WUNTRACED 2
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WTERMSIG(s)    ((s) & 0x7f)
#define WIFEXITED(s)   (WTERMSIG(s) == 0)
#define WIFSIGNALED(s) (WTERMSIG(s) != 0 && WTERMSIG(s) != 0x7f)
#define WIFSTOPPED(s)  0
#define WSTOPSIG(s)    WEXITSTATUS(s)
#define WCOREDUMP(s)   0
pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int opcoes);
#endif
