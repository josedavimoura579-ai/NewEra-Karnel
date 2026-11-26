/* nlibc - fcntl.h */
#ifndef _FCNTL_H
#define _FCNTL_H
#include <sys/types.h>
#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#define O_CREAT   0100
#define O_EXCL    0200
#define O_TRUNC   01000
#define O_APPEND  02000
#define O_CLOEXEC 02000000
#define F_GETFD   1
#define F_SETFD   2
#define F_GETFL   3
#define F_SETFL   4
#define FD_CLOEXEC 1
int open(const char *caminho, int flags, ...);
/* No NovaEra um filho só herda a entrada e a saída padrão: todo
 * descritor já se fecha num posix_spawn (FD_CLOEXEC). */
int fcntl(int fd, int cmd, ...);
#endif
