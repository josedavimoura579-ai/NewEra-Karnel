/*
 * usuario/bin/janelas.c - o servidor de janelas do NovaEra (só no modo
 * gráfico).
 *
 *   janelas                    abre e já começa três exemplos: rabisco, bloco, bolas
 *   janelas PROG...            abre e começa estes programas
 *   janelas -                  só o servidor (os programas vêm depois)
 *
 * Cada programa desenha a SUA janela numa memória compartilhada e conversa
 * com o servidor por um socket local (janela.h). O servidor só faz três
 * coisas:
 *   - COMPÕE a tela: o fundo e, por cima, cada janela, de trás para a
 *     frente, com a moldura (a barra de título, o X de fechar e a sombra);
 *   - leva o MOUSE: arrastar pela barra, trazer para a frente, fechar; os
 *     cliques dentro da janela vão para o programa dela;
 *   - leva o TECLADO para a janela da frente.
 * Se um programa morre, só a janela dele some. Ctrl+Q fecha tudo.
 *
 * Por dentro: uma thread para o mouse, uma para o teclado, uma para aceitar
 * conexões e uma para cada programa conectado. Todas mexem nas mesmas
 * janelas, então só uma de cada vez (o "mutex" g). Elas não desenham:
 * marcam o pedaço da tela que mudou e acordam a thread principal, a única
 * que compõe (o vídeo só aceita a tela de quem a abriu). Vários pedidos
 * seguidos (arrastar gera muitos) viram um desenho só.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <poll.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <grafico.h>
#include <janela.h>
#include "ulib.h"

#define MAX_JANELAS  16
#define MAX_FILHOS   16
#define BARRA        28              /* a altura da barra de título */
#define SOMBRA       6
#define CTRL_Q       17

typedef struct {
    int   usada, id, fd;
    int   x, y;                      /* o canto da moldura na tela */
    int   l, a;                      /* o tamanho do conteúdo */
    gr_imagem_t conteudo;            /* os pontos que o programa desenha */
    char  titulo[48];
} janela_t;

static pthread_mutex_t g = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  mudou = PTHREAD_COND_INITIALIZER;
#define MAX_SUJOS 8
typedef struct { int x, y, l, a; } ret_t;
static ret_t sujos[MAX_SUJOS];        /* os pedaços da tela para redesenhar */
static int n_sujos;
static gr_imagem_t *tela, *fundo;
static gr_fonte_t *negrito, *normal;
static janela_t janelas[MAX_JANELAS];
static janela_t *ordem[MAX_JANELAS];  /* ordem[0] é a de trás; ordem[n-1], a da frente */
static int n_janelas, proximo_id = 1, abertas_ate_agora;
static pid_t filhos[MAX_FILHOS];
static int n_filhos;

/* o mouse */
static janela_t *arrastando, *capturou;   /* a janela arrastada / que recebe o mouse até soltar */
static int arr_dx, arr_dy;

static int L(janela_t *j) { return j->l + 2; }              /* a moldura inteira */
static int A(janela_t *j) { return j->a + BARRA + 1; }
static janela_t *frente(void) { return n_janelas ? ordem[n_janelas - 1] : NULL; }

/* ------------------------------ Compor ---------------------------------- */

/* Copia uma imagem opaca (sem misturar: mais rápido), cortando nas bordas */
static void copiar_opaco(gr_imagem_t *d, int x, int y, const gr_imagem_t *o)
{
    int i0 = x < 0 ? -x : 0, j0 = y < 0 ? -y : 0;
    int i1 = o->largura, j1 = o->altura;
    if (x + i1 > d->largura) i1 = d->largura - x;
    if (y + j1 > d->altura) j1 = d->altura - y;
    if (i0 >= i1) return;
    for (int j = j0; j < j1; j++)
        memcpy(&d->pontos[(size_t)(y + j) * d->passo + x + i0], &o->pontos[(size_t)j * o->passo + i0],
               (size_t)(i1 - i0) * sizeof(gr_cor));
}

static void moldura(gr_imagem_t *v, janela_t *j, int x, int y)
{
    int ativa = j == frente();
    gr_preencher(v, x + SOMBRA, y + SOMBRA, L(j), A(j), GR_RGBA(0, 0, 0, 70));
    gr_degrade(v, x, y, L(j), BARRA, ativa ? GR_RGB(36, 87, 197) : GR_RGB(70, 84, 110),
               ativa ? GR_RGB(27, 58, 107) : GR_RGB(52, 62, 82), 1);
    gr_retangulo(v, x, y, L(j), A(j), ativa ? GR_CIANO : GR_RGB(90, 104, 130));
    gr_fonte_texto(v, negrito, x + 12, y + 6, j->titulo, 14, ativa ? GR_BRANCO : GR_RGB(190, 200, 215));
    int cx = x + L(j) - 16, cy = y + BARRA / 2;              /* o X de fechar */
    gr_disco(v, cx, cy, 8, ativa ? GR_RGB(220, 70, 60) : GR_RGB(110, 120, 140));
    gr_linha(v, cx - 3, cy - 3, cx + 3, cy + 3, GR_BRANCO);
    gr_linha(v, cx - 3, cy + 3, cx + 3, cy - 3, GR_BRANCO);
    copiar_opaco(v, x + 1, y + BARRA, &j->conteudo);
}

/* Redesenha um retângulo da tela e manda o vídeo mostrar (só a thread principal) */
static void desenhar(int x, int y, int l, int a)
{
    if (x < 0) { l += x; x = 0; }
    if (y < 0) { a += y; y = 0; }
    if (x + l > tela->largura) l = tela->largura - x;
    if (y + a > tela->altura) a = tela->altura - y;
    if (l <= 0 || a <= 0) return;
    /* uma "janela" na imagem da tela: desenhar nela já corta no retângulo */
    gr_imagem_t v = { tela->pontos + (size_t)y * tela->passo + x, l, a, tela->passo, 1 };
    for (int j = 0; j < a; j++)
        memcpy(&v.pontos[(size_t)j * v.passo], &fundo->pontos[(size_t)(y + j) * fundo->passo + x],
               (size_t)l * sizeof(gr_cor));
    for (int i = 0; i < n_janelas; i++) {
        janela_t *j = ordem[i];
        if (j->x >= x + l || j->y >= y + a || j->x + L(j) + SOMBRA <= x || j->y + A(j) + SOMBRA <= y) continue;
        moldura(&v, j, j->x - x, j->y - y);
    }
    gr_tela_mostrar(tela, x, y, l, a);
}

/* Marca um pedaço para redesenhar (juntando com um que já encosta nele) */
static void compor(int x, int y, int l, int a)
{
    if (l <= 0 || a <= 0) return;
    ret_t r = { x, y, l, a };
    for (int i = 0; i < n_sujos || n_sujos == MAX_SUJOS; i++) {
        if (i == n_sujos) i = 0;                          /* lotado: junta com o primeiro */
        ret_t *s = &sujos[i];
        int encosta = r.x <= s->x + s->l && s->x <= r.x + r.l && r.y <= s->y + s->a && s->y <= r.y + r.a;
        if (!encosta && n_sujos < MAX_SUJOS) continue;
        int x1 = s->x + s->l > r.x + r.l ? s->x + s->l : r.x + r.l;
        int y1 = s->y + s->a > r.y + r.a ? s->y + s->a : r.y + r.a;
        s->x = s->x < r.x ? s->x : r.x;
        s->y = s->y < r.y ? s->y : r.y;
        s->l = x1 - s->x;
        s->a = y1 - s->y;
        pthread_cond_signal(&mudou);
        return;
    }
    sujos[n_sujos++] = r;
    pthread_cond_signal(&mudou);
}

static void compor_janela(janela_t *j) { compor(j->x, j->y, L(j) + SOMBRA, A(j) + SOMBRA); }

static void fazer_fundo(void)
{
    int l = tela->largura, a = tela->altura;
    fundo = gr_nova(l, a);
    gr_degrade(fundo, 0, 0, l, a, GR_NOITE, GR_RGB(27, 58, 107), 1);
    gr_imagem_t *logo = gr_carregar("/usr/share/nefs/exemplos/imagens/logo.png");
    if (logo) { gr_escalar(fundo, l / 2 - 80, a / 2 - 130, 160, 160, logo); gr_liberar(logo); }
    int w = gr_fonte_largura(negrito, "NovaEra", 48);
    int x = (l - w) / 2;
    x += gr_fonte_texto(fundo, negrito, x, a / 2 + 40, "Nova", 48, GR_RGBA(255, 255, 255, 110));
    gr_fonte_texto(fundo, negrito, x, a / 2 + 40, "Era", 48, GR_RGBA(255, 106, 0, 130));
    gr_preencher(fundo, 0, a - 26, l, 26, GR_RGBA(0, 0, 0, 110));
    gr_fonte_texto(fundo, normal, 12, a - 21, "Arraste as janelas pela barra de título   ·   "
                   "o X fecha   ·   Ctrl+Q sai", 14, GR_RGB(159, 179, 207));
}

/* ------------------------------ Conversar ------------------------------- */

/* Manda um evento sem nunca travar o servidor: se o programa não está lendo
 * e o socket encheu, o evento se perde. */
static void enviar(janela_t *j, jn_msg_t *m)
{
    struct pollfd p = { j->fd, POLLOUT, 0 };
    if (poll(&p, 1, 0) <= 0 || !(p.revents & POLLOUT)) return;
    m->id = j->id;
    send(j->fd, m, sizeof *m, 0);
}

static void evento(janela_t *j, int tipo, int ev, int x, int y, int botoes, int tecla, int a)
{
    if (!j) return;
    jn_msg_t m;
    memset(&m, 0, sizeof m);
    m.tipo = tipo; m.evento = ev; m.x = x; m.y = y; m.botoes = botoes; m.tecla = tecla; m.a = a;
    enviar(j, &m);
}

/* Traz j para a frente (e avisa quem saiu e quem chegou) */
static void para_frente(janela_t *j)
{
    janela_t *antes = frente();
    if (antes == j) return;
    int k = 0;
    while (ordem[k] != j) k++;
    for (; k < n_janelas - 1; k++) ordem[k] = ordem[k + 1];
    ordem[n_janelas - 1] = j;
    if (antes) { evento(antes, JN_FOCO, 0, 0, 0, 0, 0, 0); compor_janela(antes); }
    evento(j, JN_FOCO, 0, 1, 0, 0, 0, 0);
    compor_janela(j);
}

static void tirar(janela_t *j)
{
    int x = j->x, y = j->y, l = L(j) + SOMBRA, a = A(j) + SOMBRA, k = 0;
    int era_frente = j == frente();
    while (ordem[k] != j) k++;
    for (; k < n_janelas - 1; k++) ordem[k] = ordem[k + 1];
    n_janelas--;
    if (arrastando == j) arrastando = NULL;
    if (capturou == j) capturou = NULL;
    munmap(j->conteudo.pontos, (size_t)j->l * (size_t)j->a * 4);
    j->usada = 0;
    compor(x, y, l, a);
    if (era_frente && frente()) {
        evento(frente(), JN_FOCO, 0, 1, 0, 0, 0, 0);
        compor_janela(frente());
    }
}

static void criar(int fd, jn_msg_t *m)
{
    jn_msg_t r;
    memset(&r, 0, sizeof r);
    r.tipo = MSG_ERRO;
    janela_t *j = NULL;
    for (int i = 0; i < MAX_JANELAS && !j; i++) if (!janelas[i].usada) j = &janelas[i];
    int maxl = tela->largura - 2, maxa = tela->altura - BARRA - 1;
    if (!j || m->l <= 0 || m->a <= 0 || m->l > maxl || m->a > maxa) { send(fd, &r, sizeof r, 0); return; }
    m->shm[sizeof m->shm - 1] = 0;
    int shm = shm_open(m->shm, O_RDWR, 0);
    void *p = shm < 0 ? MAP_FAILED : mmap(NULL, (size_t)m->l * (size_t)m->a * 4, PROT_READ, MAP_SHARED, shm, 0);
    if (shm >= 0) close(shm);
    if (p == MAP_FAILED) { send(fd, &r, sizeof r, 0); return; }
    memset(j, 0, sizeof *j);
    j->usada = 1;
    j->id = proximo_id++;
    j->fd = fd;
    j->l = m->l;
    j->a = m->a;
    j->conteudo.pontos = p;
    j->conteudo.largura = m->l;
    j->conteudo.altura = m->a;
    j->conteudo.passo = m->l;
    j->conteudo._tela = 1;
    memcpy(j->titulo, m->titulo, sizeof j->titulo);
    j->titulo[sizeof j->titulo - 1] = 0;
    /* em cascata, sem sair da tela */
    int k = abertas_ate_agora++ % 8;
    j->x = 60 + 70 * k;
    j->y = 40 + 50 * k;
    if (j->x + L(j) > tela->largura) j->x = tela->largura - L(j);
    if (j->y + A(j) > tela->altura - 26) j->y = tela->altura - 26 - A(j);
    if (j->x < 0) j->x = 0;
    if (j->y < 0) j->y = 0;
    ordem[n_janelas++] = j;
    r.tipo = JN_CRIADA;
    r.id = j->id;
    r.x = j->x;
    r.y = j->y;
    send(fd, &r, sizeof r, 0);
    janela_t *antes = n_janelas > 1 ? ordem[n_janelas - 2] : NULL;
    if (antes) { evento(antes, JN_FOCO, 0, 0, 0, 0, 0, 0); compor_janela(antes); }
    compor_janela(j);
}

static janela_t *da_conexao(int fd, int id)
{
    for (int i = 0; i < MAX_JANELAS; i++)
        if (janelas[i].usada && janelas[i].fd == fd && janelas[i].id == id) return &janelas[i];
    return NULL;
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

/* Uma thread por programa conectado */
static void *conexao(void *arg)
{
    int fd = (int)(long)arg;
    jn_msg_t m;
    while (receber(fd, &m) == 0) {
        pthread_mutex_lock(&g);
        janela_t *j = da_conexao(fd, m.id);
        if (m.tipo == JN_CRIAR) criar(fd, &m);
        else if (m.tipo == JN_MOSTRAR && j) {
            int x = m.x < 0 ? 0 : m.x, y = m.y < 0 ? 0 : m.y;
            int x1 = m.x + m.l > j->l ? j->l : m.x + m.l, y1 = m.y + m.a > j->a ? j->a : m.y + m.a;
            if (x1 > x && y1 > y) compor(j->x + 1 + x, j->y + BARRA + y, x1 - x, y1 - y);
        } else if (m.tipo == JN_TITULO && j) {
            memcpy(j->titulo, m.titulo, sizeof j->titulo);
            j->titulo[sizeof j->titulo - 1] = 0;
            compor(j->x, j->y, L(j), BARRA);
        }
        pthread_mutex_unlock(&g);
    }
    /* o programa fechou (ou morreu): as janelas dele somem */
    pthread_mutex_lock(&g);
    for (int i = 0; i < MAX_JANELAS; i++) if (janelas[i].usada && janelas[i].fd == fd) tirar(&janelas[i]);
    pthread_mutex_unlock(&g);
    close(fd);
    return NULL;
}

/* ------------------------------ Mouse e teclado ------------------------- */

static janela_t *embaixo(int x, int y)
{
    for (int i = n_janelas - 1; i >= 0; i--) {
        janela_t *j = ordem[i];
        if (x >= j->x && x < j->x + L(j) && y >= j->y && y < j->y + A(j)) return j;
    }
    return NULL;
}

static void mover(janela_t *j, int x, int y)
{
    if (x < 40 - L(j)) x = 40 - L(j);                 /* um pedaço da barra fica sempre na tela */
    if (x > tela->largura - 40) x = tela->largura - 40;
    if (y < 0) y = 0;
    if (y > tela->altura - BARRA) y = tela->altura - BARRA;
    if (x == j->x && y == j->y) return;
    int x0 = j->x, y0 = j->y;
    j->x = x;
    j->y = y;
    int ex = x < x0 ? x : x0, ey = y < y0 ? y : y0;   /* o lugar velho e o novo juntos */
    int dx = x > x0 ? x - x0 : x0 - x, dy = y > y0 ? y - y0 : y0 - y;
    compor(ex, ey, L(j) + SOMBRA + dx, A(j) + SOMBRA + dy);
}

static void mouse(int x, int y, int b, int roda)
{
    static int antes;
    int apertou = b & ~antes, soltou = antes & ~b;
    antes = b;
    if (arrastando) {
        if (b & GR_ESQUERDO) mover(arrastando, x - arr_dx, y - arr_dy);
        else arrastando = NULL;
        return;
    }
    if (capturou) {                                     /* o botão foi apertado dentro dela */
        janela_t *j = capturou;
        evento(j, JN_MOUSE, soltou ? JN_SOLTOU : apertou ? JN_APERTOU : JN_MOVEU,
               x - j->x - 1, y - j->y - BARRA, b, 0, 0);
        if (!b) capturou = NULL;
        return;
    }
    janela_t *j = embaixo(x, y);
    if (roda && j && y >= j->y + BARRA)
        evento(j, JN_MOUSE, JN_RODA, x - j->x - 1, y - j->y - BARRA, b, 0, roda);
    if (!apertou || !j) return;
    para_frente(j);
    if (y < j->y + BARRA) {                             /* na barra de título */
        int cx = j->x + L(j) - 16, cy = j->y + BARRA / 2;
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= 81) evento(j, JN_FECHAR, 0, 0, 0, 0, 0, 0);
        else if (apertou & GR_ESQUERDO) { arrastando = j; arr_dx = x - j->x; arr_dy = y - j->y; }
        return;
    }
    capturou = j;
    evento(j, JN_MOUSE, JN_APERTOU, x - j->x - 1, y - j->y - BARRA, b, 0, 0);
}

static void *thread_mouse(void *x)
{
    (void)x;
    for (;;) {
        int mx, my, b;
        int roda = gr_mouse(&mx, &my, &b);
        pthread_mutex_lock(&g);
        mouse(mx, my, b, roda);
        pthread_mutex_unlock(&g);
    }
    return NULL;
}

static void terminal_cru(int ligar)
{
    msg_t m;
    msg_zera(&m);
    m.tipo = TERM_CRU;
    m.arg[0] = (u32)ligar;
    sys_chamar(PID_TERMINAL, &m);
}

static void sair(int codigo)
{
    for (int i = 0; i < n_filhos; i++) kill(filhos[i], SIGTERM);
    terminal_cru(0);
    gr_tela_fechar(tela);
    exit(codigo);
}

static void *thread_teclado(void *x)
{
    (void)x;
    char c;
    while (read(0, &c, 1) == 1) {
        pthread_mutex_lock(&g);
        if (c == CTRL_Q) sair(0);
        if (c == '\r') c = '\n';
        evento(frente(), JN_TECLA, 0, 0, 0, 0, (unsigned char)c, 0);
        pthread_mutex_unlock(&g);
    }
    return NULL;
}

/* ------------------------------ Começo ---------------------------------- */

static void *thread_aceitar(void *arg)
{
    int ouvir = (int)(long)arg;
    for (;;) {
        int fd = accept(ouvir, NULL, NULL);
        if (fd < 0) continue;
        pthread_t t;
        pthread_create(&t, NULL, conexao, (void *)(long)fd);
        pthread_detach(t);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    negrito = gr_fonte_abrir(GR_FONTE_NEGRITO);
    normal = gr_fonte_abrir(GR_FONTE_NORMAL);
    if (!negrito || !normal) { fprintf(stderr, "janelas: não achei as fontes em /usr/share/fonts/dejavu\n"); return 1; }

    int ouvir = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un end;
    memset(&end, 0, sizeof end);
    end.sun_family = AF_UNIX;
    strcpy(end.sun_path, JN_SOCKET);
    if (ouvir < 0 || bind(ouvir, (struct sockaddr *)&end, sizeof end) < 0 || listen(ouvir, 8) < 0) {
        fprintf(stderr, "janelas: não consegui ouvir em %s (já tem um servidor de janelas?)\n", JN_SOCKET);
        return 1;
    }
    tela = gr_tela_abrir();
    if (!tela) { fprintf(stderr, "janelas: só no modo gráfico (bash run.sh grafico ou vnc)\n"); return 1; }
    fazer_fundo();
    desenhar(0, 0, tela->largura, tela->altura);
    terminal_cru(1);
    signal(SIGINT, sair);
    signal(SIGTERM, sair);

    pthread_t t;
    pthread_create(&t, NULL, thread_mouse, NULL);
    pthread_create(&t, NULL, thread_teclado, NULL);

    /* os programas para começar */
    static char *padrao[] = { "janelas", "rabisco", "bloco", "bolas", NULL };
    char **progs = argc > 1 ? argv : padrao;
    for (int i = 1; progs[i] && n_filhos < MAX_FILHOS; i++) {
        if (!strcmp(progs[i], "-")) continue;
        char *av[] = { progs[i], NULL };
        if (posix_spawnp(&filhos[n_filhos], progs[i], NULL, NULL, av, NULL) == 0) n_filhos++;
    }

    pthread_create(&t, NULL, thread_aceitar, (void *)(long)ouvir);

    /* a thread principal desenha o que as outras marcaram */
    pthread_mutex_lock(&g);
    for (;;) {
        while (!n_sujos) pthread_cond_wait(&mudou, &g);
        ret_t r = sujos[--n_sujos];
        desenhar(r.x, r.y, r.l, r.a);
    }
}
