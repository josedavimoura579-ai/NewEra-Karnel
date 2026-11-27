/* nlibc - poll.h: esperar até algum descritor ter o que ler (ou espaço
 * para escrever). Nos sockets, pergunta ao servidor (SOCK_ESTADO); os
 * arquivos estão sempre prontos. */
#ifndef _POLL_H
#define _POLL_H
struct pollfd {
    int   fd;
    short events;
    short revents;
};
typedef unsigned long nfds_t;
#define POLLIN   0x001
#define POLLOUT  0x004
#define POLLERR  0x008
#define POLLHUP  0x010
#define POLLNVAL 0x020
int poll(struct pollfd *fds, nfds_t n, int espera_ms);
#endif
