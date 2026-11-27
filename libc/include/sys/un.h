/* nlibc - sys/un.h: o endereço de um socket local (um nome, como um caminho). */
#ifndef _SYS_UN_H
#define _SYS_UN_H
#include <sys/socket.h>
struct sockaddr_un {
    sa_family_t sun_family;        /* AF_UNIX */
    char        sun_path[108];     /* "/tmp/servidor.sock" (no NovaEra, até 63 letras) */
};
#endif
