/* nlibc - sys/socket.h: sockets locais (AF_UNIX), pelo servidor de
 * sockets do NovaEra (PID 11). */
#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H
#include <sys/types.h>

typedef unsigned int socklen_t;
typedef unsigned short sa_family_t;

struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};

#define AF_UNSPEC   0
#define AF_UNIX     1
#define AF_LOCAL    AF_UNIX
#define PF_UNIX     AF_UNIX
#define AF_INET     2              /* (rede: ainda não) */
#define SOCK_STREAM 1
#define SOCK_DGRAM  2              /* (ainda não) */
#define SOCK_CLOEXEC  02000000
#define SOCK_NONBLOCK 04000
#define SOL_SOCKET  1
#define SO_REUSEADDR 2
#define SO_ERROR    4
#define SOMAXCONN   16
#define SHUT_RD     0
#define SHUT_WR     1
#define SHUT_RDWR   2
#define MSG_NOSIGNAL 0x4000

int     socket(int dominio, int tipo, int protocolo);
int     socketpair(int dominio, int tipo, int protocolo, int fds[2]);
int     bind(int fd, const struct sockaddr *end, socklen_t tam);
int     listen(int fd, int fila);
int     accept(int fd, struct sockaddr *end, socklen_t *tam);
int     connect(int fd, const struct sockaddr *end, socklen_t tam);
ssize_t send(int fd, const void *buf, size_t n, int flags);
ssize_t recv(int fd, void *buf, size_t n, int flags);
int     shutdown(int fd, int como);
int     getsockopt(int fd, int nivel, int opcao, void *valor, socklen_t *tam);
int     setsockopt(int fd, int nivel, int opcao, const void *valor, socklen_t tam);
#endif
