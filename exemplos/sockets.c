/*
 * exemplos/sockets.c - um servidor e três clientes conversando por sockets.
 *
 *   cproc -o /tmp/sockets /usr/share/nefs/exemplos/sockets.c
 *   /tmp/sockets
 *
 * O servidor ouve no nome "/tmp/maiusculas.sock" e roda três cópias de si
 * mesmo como clientes. Cada cliente se conecta, manda uma frase e recebe a
 * frase de volta EM MAIÚSCULAS. O servidor atende os três ao mesmo tempo:
 * o poll() diz qual deles tem algo para ler. É assim que o servidor de
 * janelas vai falar com cada programa.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <spawn.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#define NOME "/tmp/maiusculas.sock"

static struct sockaddr_un endereco(void)
{
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strcpy(a.sun_path, NOME);
    return a;
}

static int cliente(const char *frase)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = endereco();
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) { perror("connect"); return 1; }
    write(fd, frase, strlen(frase));
    char resposta[128];
    int n = (int)read(fd, resposta, sizeof resposta - 1);
    resposta[n > 0 ? n : 0] = '\0';
    printf("  cliente %d mandou \"%s\" e recebeu \"%s\"\n", getpid(), frase, resposta);
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "cliente")) return cliente(argv[2]);

    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = endereco();
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0 || listen(s, 8) < 0) { perror("bind"); return 1; }
    printf("Servidor (processo %d) ouvindo em %s\n", getpid(), NOME);

    const char *frases[] = { "ola, servidor", "tudo bem?", "sockets sao legais" };
    pid_t filhos[3];
    for (int i = 0; i < 3; i++) {
        char *args[] = { argv[0], "cliente", (char *)frases[i], NULL };
        posix_spawn(&filhos[i], argv[0], NULL, NULL, args, NULL);
    }

    struct pollfd fds[4];
    int n = 1, atendidos = 0;
    fds[0].fd = s;                                /* o 0 é o que ouve: conexões novas */
    fds[0].events = POLLIN;
    while (atendidos < 3) {
        poll(fds, (nfds_t)n, -1);
        if (fds[0].revents & POLLIN) {            /* alguém conectou */
            fds[n].fd = accept(s, NULL, NULL);
            fds[n].events = POLLIN;
            n++;
        }
        for (int i = 1; i < n; i++) {
            if (fds[i].fd < 0 || !(fds[i].revents & POLLIN)) continue;
            char buf[128];
            int k = (int)read(fds[i].fd, buf, sizeof buf);
            for (int j = 0; j < k; j++) buf[j] = (char)toupper((unsigned char)buf[j]);
            if (k > 0) write(fds[i].fd, buf, (size_t)k);
            close(fds[i].fd);
            fds[i].fd = -1;                       /* este já foi */
            atendidos++;
        }
    }
    for (int i = 0; i < 3; i++) waitpid(filhos[i], NULL, 0);
    close(s);
    printf("Servidor: atendi %d clientes.\n", atendidos);
    return 0;
}
