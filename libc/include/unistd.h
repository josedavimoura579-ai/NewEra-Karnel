/* nlibc - unistd.h: o pedaço do POSIX que o NovaEra já tem. */
#ifndef _UNISTD_H
#define _UNISTD_H
#include <sys/types.h>
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int     close(int fd);
int     pipe(int fds[2]);
int     dup(int fd);
int     dup2(int fd, int novo);
int     unlink(const char *caminho);
int     rmdir(const char *caminho);
int     chdir(const char *caminho);
char   *getcwd(char *buf, size_t tam);
int     isatty(int fd);
pid_t   getpid(void);
int     access(const char *caminho, int modo);
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

extern char *optarg;
extern int   optind, opterr, optopt;
int getopt(int argc, char *const argv[], const char *opcoes);
#endif
