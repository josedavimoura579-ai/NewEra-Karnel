/*
 * nlibc - libc/src/janela.c
 * O lado do programa na conversa com o servidor de janelas (janela.h):
 * cria a memória compartilhada da janela, conecta no socket e troca as
 * mensagens jn_msg_t.
 */
#include <janela.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>

static int enviar(int fd, const jn_msg_t *m)
{
    const char *p = (const char *)m;
    size_t falta = sizeof *m;
    while (falta) {
        ssize_t n = send(fd, p, falta, 0);
        if (n <= 0) return -1;
        p += n;
        falta -= (size_t)n;
    }
    return 0;
}

static int receber(int fd, jn_msg_t *m)
{
    char *p = (char *)m;
    size_t falta = sizeof *m;
    while (falta) {
        ssize_t n = recv(fd, p, falta, 0);
        if (n <= 0) return -1;
        p += n;
        falta -= (size_t)n;
    }
    return 0;
}

jn_janela_t *jn_abrir(const char *titulo, int largura, int altura)
{
    static int conta;
    if (largura <= 0 || altura <= 0) return NULL;
    jn_janela_t *j = calloc(1, sizeof *j);
    if (!j) return NULL;
    j->largura = largura;
    j->altura = altura;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un end;
    memset(&end, 0, sizeof end);
    end.sun_family = AF_UNIX;
    strcpy(end.sun_path, JN_SOCKET);
    if (fd < 0 || connect(fd, (struct sockaddr *)&end, sizeof end) < 0) {
        if (fd >= 0) close(fd);
        free(j);
        return NULL;
    }
    j->fd = fd;

    /* a memória da janela: largura x altura pontos */
    jn_msg_t m;
    memset(&m, 0, sizeof m);
    snprintf(m.shm, sizeof m.shm, "/janela-%d-%d", (int)getpid(), conta++);
    size_t tam = (size_t)largura * (size_t)altura * 4;
    int shm = shm_open(m.shm, O_CREAT | O_RDWR, 0600);
    void *pontos = MAP_FAILED;
    if (shm >= 0 && ftruncate(shm, (off_t)tam) == 0)
        pontos = mmap(NULL, tam, PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);
    if (pontos == MAP_FAILED) {
        if (shm >= 0) { close(shm); shm_unlink(m.shm); }
        close(fd);
        free(j);
        return NULL;
    }
    j->im = calloc(1, sizeof *j->im);
    j->im->pontos = pontos;
    j->im->largura = largura;
    j->im->altura = altura;
    j->im->passo = largura;
    j->im->_tela = 1;                       /* (os pontos não são do malloc) */
    gr_preencher(j->im, 0, 0, largura, altura, GR_BRANCO);

    m.tipo = JN_CRIAR;
    m.l = largura;
    m.a = altura;
    snprintf(m.titulo, sizeof m.titulo, "%s", titulo ? titulo : "");
    jn_msg_t r;
    int ok = enviar(fd, &m) == 0 && receber(fd, &r) == 0 && r.tipo == JN_CRIADA;
    shm_unlink(m.shm);                      /* o servidor já mapeou (ou desistiu): o nome não serve mais */
    close(shm);
    if (!ok) {
        munmap(pontos, tam);
        close(fd);
        free(j->im);
        free(j);
        return NULL;
    }
    j->id = r.id;
    return j;
}

void jn_mostrar(jn_janela_t *j, int x, int y, int l, int a)
{
    if (!j) return;
    jn_msg_t m;
    memset(&m, 0, sizeof m);
    m.tipo = JN_MOSTRAR;
    m.id = j->id;
    m.x = x; m.y = y; m.l = l; m.a = a;
    enviar(j->fd, &m);
}

void jn_mostrar_tudo(jn_janela_t *j) { if (j) jn_mostrar(j, 0, 0, j->largura, j->altura); }

void jn_titulo(jn_janela_t *j, const char *titulo)
{
    if (!j) return;
    jn_msg_t m;
    memset(&m, 0, sizeof m);
    m.tipo = JN_TITULO;
    m.id = j->id;
    snprintf(m.titulo, sizeof m.titulo, "%s", titulo ? titulo : "");
    enviar(j->fd, &m);
}

int jn_esperar(jn_janela_t *j, jn_evento_t *e)
{
    return j && receber(j->fd, e) == 0;
}

int jn_tem_evento(jn_janela_t *j)
{
    if (!j) return 0;
    struct pollfd p = { j->fd, POLLIN, 0 };
    return poll(&p, 1, 0) > 0 && (p.revents & (POLLIN | POLLHUP));
}

void jn_fechar(jn_janela_t *j)
{
    if (!j) return;
    close(j->fd);
    munmap(j->im->pontos, (size_t)j->largura * (size_t)j->altura * 4);
    free(j->im);
    free(j);
}
