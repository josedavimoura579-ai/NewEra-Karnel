/*
 * nlibc - libc/src/sockets.c
 * socket, bind, listen, accept, connect, socketpair, send, recv e poll.
 * Cada socket é um descritor de arquivo com o servidor PID_SOCKETS: o
 * read, o write e o close de sempre funcionam nele.
 */
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "interno.h"

static long pedir(u32 tipo, u32 canal, u32 a1, const char *texto, msg_t *m)
{
    msg_zera(m);
    m->tipo = tipo;
    m->arg[0] = canal;
    m->arg[1] = a1;
    if (texto) str_copia(m->texto, texto, MSG_TEXTO_MAX);
    long r = sys_chamar(PID_SOCKETS, m);
    if (r < 0) return r == ERRO_INTERROMPIDO ? r : ERRO_PID;
    return m->tipo == MSG_ERRO ? -(long)m->arg[0] : 0;
}

static int erro_de(long r)
{
    switch (r) {
    case ERRO_NAO_ACHADO:   return ECONNREFUSED;
    case ERRO_EXISTE:       return EADDRINUSE;
    case ERRO_LOTADO:       return EAGAIN;
    case ERRO_INTERROMPIDO: return EINTR;
    case ERRO_TUBO:         return EPIPE;
    }
    return EINVAL;
}

/* O canal leva o servidor nos bits 8-15, como o dos arquivos (o close usa). */
static u32 canal_de(u32 id) { return (u32)PID_SOCKETS << 8 | id; }

static nfd_t *socket_de(int fd)
{
    nfd_t *f = __fd(fd);
    if (f && f->servidor != PID_SOCKETS) { errno = ENOTSOCK; return NULL; }
    return f;
}

int socket(int dominio, int tipo, int protocolo)
{
    (void)protocolo;
    if (dominio != AF_UNIX) { errno = EAFNOSUPPORT; return -1; }
    if ((tipo & 0xF) != SOCK_STREAM) { errno = EPROTONOSUPPORT; return -1; }
    msg_t m;
    long r = pedir(SOCK_CRIAR, 0, 0, NULL, &m);
    if (r < 0) { errno = erro_de(r); return -1; }
    return __fd_novo(PID_SOCKETS, canal_de(m.arg[0]), 0, 1);
}

int socketpair(int dominio, int tipo, int protocolo, int fds[2])
{
    (void)protocolo;
    if (dominio != AF_UNIX || (tipo & 0xF) != SOCK_STREAM) { errno = EAFNOSUPPORT; return -1; }
    msg_t m;
    long r = pedir(SOCK_PAR, 0, 0, NULL, &m);
    if (r < 0) { errno = erro_de(r); return -1; }
    fds[0] = __fd_novo(PID_SOCKETS, canal_de(m.arg[0]), 0, 1);
    fds[1] = __fd_novo(PID_SOCKETS, canal_de(m.arg[1]), 0, 1);
    return fds[0] < 0 || fds[1] < 0 ? -1 : 0;
}

static const char *nome_de(const struct sockaddr *end, socklen_t tam)
{
    const struct sockaddr_un *u = (const struct sockaddr_un *)end;
    if (!end || tam < sizeof(sa_family_t) + 1 || u->sun_family != AF_UNIX || !u->sun_path[0]) return NULL;
    if (strlen(u->sun_path) >= MSG_TEXTO_MAX) return NULL;
    return u->sun_path;
}

int bind(int fd, const struct sockaddr *end, socklen_t tam)
{
    nfd_t *f = socket_de(fd);
    const char *nome = nome_de(end, tam);
    if (!f) return -1;
    if (!nome) { errno = EINVAL; return -1; }
    msg_t m;
    long r = pedir(SOCK_LIGAR, f->canal, 0, nome, &m);
    if (r < 0) { errno = erro_de(r); return -1; }
    return 0;
}

int listen(int fd, int fila)
{
    nfd_t *f = socket_de(fd);
    if (!f) return -1;
    msg_t m;
    long r = pedir(SOCK_OUVIR, f->canal, fila > 0 ? (u32)fila : SOMAXCONN, NULL, &m);
    if (r < 0) { errno = erro_de(r); return -1; }
    return 0;
}

int accept(int fd, struct sockaddr *end, socklen_t *tam)
{
    nfd_t *f = socket_de(fd);
    if (!f) return -1;
    msg_t m;
    long r = pedir(SOCK_ACEITAR, f->canal, 0, NULL, &m);
    if (r < 0) { errno = erro_de(r); return -1; }
    if (end && tam && *tam >= sizeof(sa_family_t)) {
        end->sa_family = AF_UNIX;                       /* o cliente não tem nome */
        *tam = sizeof(sa_family_t);
    }
    return __fd_novo(PID_SOCKETS, canal_de(m.arg[0]), 0, 1);
}

int connect(int fd, const struct sockaddr *end, socklen_t tam)
{
    nfd_t *f = socket_de(fd);
    const char *nome = nome_de(end, tam);
    if (!f) return -1;
    if (!nome) { errno = EINVAL; return -1; }
    msg_t m;
    long r = pedir(SOCK_CONECTAR, f->canal, 0, nome, &m);
    if (r < 0) { errno = erro_de(r); return -1; }
    return 0;
}

ssize_t send(int fd, const void *buf, size_t n, int flags)
{
    if (!(flags & MSG_NOSIGNAL)) return write(fd, buf, n);
    void (*antes)(int) = signal(SIGPIPE, SIG_IGN);
    ssize_t r = write(fd, buf, n);
    signal(SIGPIPE, antes);
    return r;
}

ssize_t recv(int fd, void *buf, size_t n, int flags) { (void)flags; return read(fd, buf, n); }

int shutdown(int fd, int como) { (void)fd; (void)como; return 0; }

int getsockopt(int fd, int nivel, int opcao, void *valor, socklen_t *tam)
{
    (void)fd; (void)nivel; (void)opcao;
    if (valor && tam && *tam >= sizeof(int)) { *(int *)valor = 0; *tam = sizeof(int); }
    return 0;
}

int setsockopt(int fd, int nivel, int opcao, const void *valor, socklen_t tam)
{
    (void)fd; (void)nivel; (void)opcao; (void)valor; (void)tam;
    return 0;
}

/* poll: pergunta a cada socket se está pronto; espera passando a vez. */
int poll(struct pollfd *fds, nfds_t n, int espera_ms)
{
    u32 ini = sys_tempo();
    for (;;) {
        int prontos = 0;
        for (nfds_t i = 0; i < n; i++) {
            fds[i].revents = 0;
            nfd_t *f = __fd(fds[i].fd);
            if (!f) { fds[i].revents = POLLNVAL; prontos++; continue; }
            if (f->servidor != PID_SOCKETS) {           /* arquivos, terminal, pipes: prontos */
                fds[i].revents = fds[i].events & (POLLIN | POLLOUT);
            } else {
                msg_t m;
                if (pedir(SOCK_ESTADO, f->canal, 0, NULL, &m) < 0) { fds[i].revents = POLLERR; prontos++; continue; }
                if ((fds[i].events & POLLIN) && (m.arg[0] || m.arg[1] || m.arg[2])) fds[i].revents |= POLLIN;
                if ((fds[i].events & POLLOUT) && m.arg[3]) fds[i].revents |= POLLOUT;
                if (m.arg[2]) fds[i].revents |= POLLHUP;
            }
            if (fds[i].revents) prontos++;
        }
        if (prontos) return prontos;
        if (espera_ms >= 0 && (sys_tempo() - ini) * 10 >= (u32)espera_ms) return 0;
        sys_ceder();
    }
}
