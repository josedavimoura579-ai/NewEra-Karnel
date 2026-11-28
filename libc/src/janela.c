/*
 * nlibc - libc/src/janela.c
 * O lado do programa na conversa com o servidor de janelas (janela.h):
 * cria a memória compartilhada da janela, conecta no socket e troca as
 * mensagens jn_msg_t. Os controles (botões...) estão em janela-controles.c.
 *
 * Redimensionar: o servidor manda o tamanho novo; aqui se cria uma memória
 * nova (o programa redesenha nela) e, no próximo jn_mostrar, ela vai para o
 * servidor (JN_TROCAR). A velha só é solta quando o servidor confirma
 * (JN_TROCADA): até lá, ele ainda pode estar copiando dela.
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

/* Cria a memória compartilhada de uma imagem l x a; o nome vai em nome */
static void *memoria_nova(int l, int a, char *nome, size_t max_nome)
{
    static int conta;
    snprintf(nome, max_nome, "/janela-%d-%d", (int)getpid(), conta++);
    size_t tam = (size_t)l * (size_t)a * 4;
    int shm = shm_open(nome, O_CREAT | O_RDWR, 0600);
    void *p = MAP_FAILED;
    if (shm >= 0 && ftruncate(shm, (off_t)tam) == 0) p = mmap(NULL, tam, PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);
    if (shm >= 0) close(shm);
    if (p == MAP_FAILED) { shm_unlink(nome); return NULL; }
    return p;
}

static void apontar(jn_janela_t *j, void *pontos, int l, int a)
{
    j->im->pontos = pontos;
    j->im->largura = l;
    j->im->altura = a;
    j->im->passo = l;
    j->largura = l;
    j->altura = a;
}

jn_janela_t *jn_abrir(const char *titulo, int largura, int altura) { return jn_abrir_com(titulo, largura, altura, 0); }

jn_janela_t *jn_abrir_com(const char *titulo, int largura, int altura, int opcoes)
{
    if (largura <= 0 || altura <= 0) return NULL;
    jn_janela_t *j = calloc(1, sizeof *j);
    if (!j) return NULL;
    j->fundo = GR_RGB(240, 242, 246);

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

    jn_msg_t m;
    memset(&m, 0, sizeof m);
    void *pontos = memoria_nova(largura, altura, m.shm, sizeof m.shm);
    j->im = calloc(1, sizeof *j->im);
    if (!pontos || !j->im) {
        close(fd);
        free(j->im);
        free(j);
        return NULL;
    }
    j->im->_tela = 1;                       /* (os pontos não são do malloc) */
    apontar(j, pontos, largura, altura);
    gr_preencher(j->im, 0, 0, largura, altura, j->fundo);

    m.tipo = JN_CRIAR;
    m.l = largura;
    m.a = altura;
    m.botoes = opcoes;
    snprintf(m.titulo, sizeof m.titulo, "%s", titulo ? titulo : "");
    jn_msg_t r;
    int ok = enviar(fd, &m) == 0 && receber(fd, &r) == 0 && r.tipo == JN_CRIADA;
    shm_unlink(m.shm);                      /* o servidor já mapeou (ou desistiu): o nome não serve mais */
    if (!ok) {
        munmap(pontos, (size_t)largura * (size_t)altura * 4);
        close(fd);
        free(j->im);
        free(j);
        return NULL;
    }
    j->id = r.id;
    return j;
}

/* Depois de redimensionar: manda a memória nova (o programa já desenhou nela) */
static void trocar(jn_janela_t *j)
{
    if (!j->troca_pendente) return;
    j->troca_pendente = 0;
    jn_msg_t m;
    memset(&m, 0, sizeof m);
    m.tipo = JN_TROCAR;
    m.id = j->id;
    m.l = j->largura;
    m.a = j->altura;
    memcpy(m.shm, j->shm_novo, sizeof m.shm);
    enviar(j->fd, &m);
}

/* O servidor pediu um tamanho novo: uma imagem nova, já com o fundo */
static void redimensionar(jn_janela_t *j, int l, int a)
{
    char nome[32];
    void *p = memoria_nova(l, a, nome, sizeof nome);
    if (!p) return;
    void *velho = j->im->pontos;
    size_t velho_tam = (size_t)j->largura * (size_t)j->altura * 4;
    if (j->troca_pendente) {                 /* a anterior o servidor nunca viu: pode ir embora */
        munmap(velho, velho_tam);
        shm_unlink(j->shm_novo);
    } else if (j->n_velhos < 4) {            /* esta o servidor ainda mostra, até trocar */
        j->velhos[j->n_velhos] = velho;
        j->velhos_tam[j->n_velhos++] = velho_tam;
    }
    memcpy(j->shm_novo, nome, sizeof j->shm_novo);
    j->troca_pendente = 1;
    apontar(j, p, l, a);
    gr_preencher(j->im, 0, 0, l, a, j->fundo);
}

void jn_mostrar(jn_janela_t *j, int x, int y, int l, int a)
{
    if (!j) return;
    trocar(j);
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
    if (!j) return 0;
    for (;;) {
        if (receber(j->fd, e) < 0) return 0;
        if (e->tipo == JN_TROCADA) {            /* a memória mais velha não é mais usada */
            if (j->n_velhos) {
                munmap(j->velhos[0], j->velhos_tam[0]);
                for (int i = 1; i < j->n_velhos; i++) { j->velhos[i - 1] = j->velhos[i]; j->velhos_tam[i - 1] = j->velhos_tam[i]; }
                j->n_velhos--;
            }
            continue;
        }
        if (e->tipo == JN_REDIMENSIONAR) {
            if (e->l == j->largura && e->a == j->altura) continue;
            redimensionar(j, e->l, e->a);
        }
        return 1;
    }
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
    for (int i = 0; i < j->n_velhos; i++) munmap(j->velhos[i], j->velhos_tam[i]);
    if (j->troca_pendente) shm_unlink(j->shm_novo);
    free(j->im);
    free(j);
}

/* ------------------------------- As teclas -------------------------------- */

int jn_utf8(int u, char *o)
{
    if (u < 0x80) { o[0] = (char)u; return 1; }
    if (u < 0x800) { o[0] = (char)(0xC0 | u >> 6); o[1] = (char)(0x80 | (u & 63)); return 2; }
    o[0] = (char)(0xE0 | u >> 12); o[1] = (char)(0x80 | ((u >> 6) & 63)); o[2] = (char)(0x80 | (u & 63));
    return 3;
}

static const struct { const char *seq; int tecla; } especiais[] = {
    { "[A", JN_CIMA }, { "[B", JN_BAIXO }, { "[C", JN_DIREITA_T }, { "[D", JN_ESQUERDA_T },
    { "[H", JN_INICIO }, { "[F", JN_FIM }, { "[1~", JN_INICIO }, { "[4~", JN_FIM },
    { "[2~", JN_INSERT }, { "[3~", JN_DELETE }, { "[5~", JN_PG_CIMA }, { "[6~", JN_PG_BAIXO },
    { "OP", JN_F(1) }, { "OQ", JN_F(2) }, { "OR", JN_F(3) }, { "OS", JN_F(4) },
    { "[15~", JN_F(5) }, { "[17~", JN_F(6) }, { "[18~", JN_F(7) }, { "[19~", JN_F(8) },
    { "[20~", JN_F(9) }, { "[21~", JN_F(10) }, { "[23~", JN_F(11) }, { "[24~", JN_F(12) },
    { "OH", JN_INICIO }, { "OF", JN_FIM }, { 0, 0 } };

int jn_teclas(const char *b, int n, int *teclas, int *mods, int max)
{
    int k = 0;
    for (int i = 0; i < n && k < max; ) {
        unsigned char c = (unsigned char)b[i];
        int mod = 0;
        if (c == 27 && i + 1 < n) {
            unsigned char d = (unsigned char)b[i + 1];
            if (d == '[' || d == 'O') {                       /* Esc [ ... : uma tecla especial */
                int j = i + 2;
                while (j < n && !((unsigned char)b[j] >= 0x40 && (unsigned char)b[j] <= 0x7E)) j++;
                char seq[8];
                int l = j - i;                                /* sem o Esc, com o último */
                if (l > 7) l = 7;
                memcpy(seq, b + i + 1, (size_t)l);
                seq[l] = 0;
                int t = 0;
                for (int e = 0; especiais[e].seq; e++) if (!strcmp(especiais[e].seq, seq)) t = especiais[e].tecla;
                if (t) { teclas[k] = t; mods[k++] = 0; }
                i = j + 1;
                continue;
            }
            mod = JN_ALT;                                     /* Esc e uma tecla: Alt+tecla */
            i++;
            c = (unsigned char)b[i];
        }
        int u = c, extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (extra) u = c & (0x3F >> extra);
        i++;
        for (int e = 0; e < extra && i < n && ((unsigned char)b[i] & 0xC0) == 0x80; e++, i++)
            u = u << 6 | ((unsigned char)b[i] & 63);
        if (u == '\r') u = '\n';
        teclas[k] = u;
        mods[k++] = mod;
    }
    return k;
}

int jn_tecla_bytes(int tecla, int mods, char *o)
{
    int k = 0;
    for (int e = 0; especiais[e].seq; e++)
        if (especiais[e].tecla == tecla) {
            o[k++] = 27;
            for (const char *p = especiais[e].seq; *p; p++) o[k++] = *p;
            return k;
        }
    if (tecla >= 0xE000 && tecla < 0xF900) return 0;          /* uma especial que não tem bytes */
    if (mods & JN_ALT) o[k++] = 27;
    return k + jn_utf8(tecla, o + k);
}
