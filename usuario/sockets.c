/*
 * ===========================================================================
 *  NovaEra - usuario/sockets.c   (PID 11, anel 3)
 *  Sockets locais: o AF_UNIX do Linux, a base do X11, do Wayland e do D-Bus.
 *
 *  Um programa "servidor" cria um socket, dá um nome a ele (bind:
 *  "/tmp/jogo.sock") e fica ouvindo (listen). Um "cliente" se conecta a
 *  esse nome (connect). O servidor aceita (accept) e ganha um socket novo,
 *  só daquela conexão. Daí em diante os dois lados leem e escrevem como
 *  num pipe, mas nos dois sentidos.
 *
 *  Cada lado de uma conexão tem o seu buffer de chegada (SOCK_BUF bytes):
 *  o que um escreve vai para o buffer do outro. Como nos pipes, o servidor
 *  nunca bloqueia: quem lê um buffer vazio, ou escreve num cheio, fica
 *  esperando a resposta, que vem quando houver dados (ou espaço).
 *
 *  O nome mora aqui, não no disco (no Linux, aparece um arquivo do tipo
 *  socket na pasta).
 * ===========================================================================
 */
#include "ulib.h"

#define MAX_SOCK  64u
#define FILA_MAX  16u

enum { NOVO, OUVINDO, CONECTADO };

typedef struct {
    int  usado;
    u32  dono;                         /* o processo que o usa */
    int  tipo;
    char nome[CAMINHO_MAX];            /* se fez bind */
    int  par;                          /* o outro lado da conexão (-1) */
    int  fim;                          /* o outro lado fechou */

    u32  ini, n;                       /* o buffer (buf, no fim da struct) */
    u32  leitor, leitor_max, leitor_buf;               /* leitura esperando (0 = nenhuma) */
    u32  escritor, escritor_end, escritor_tam;          /* escrita esperando ESPAÇO neste buffer */
    char escritor_txt[MSG_TEXTO_MAX];

    int  fila[FILA_MAX];               /* conexões ainda não aceitas */
    u32  fila_n, fila_max;
    u32  aceitador;                    /* accept esperando (0 = nenhum) */
    char buf[SOCK_BUF];                /* o que chegou e ainda não foi lido (por último: novo() não o zera) */
} sock_t;

static sock_t socks[MAX_SOCK];
static u32 pedidos;

static void responder(u32 para, u32 tipo, u32 a0, u32 a1)
{
    msg_t r;
    msg_zera(&r);
    r.tipo = tipo;
    r.arg[0] = a0;
    r.arg[1] = a1;
    sys_responder(para, &r);
}

static void ok(u32 para, u32 a0) { responder(para, MSG_OK, a0, 0); }
static void erro(u32 para, long e) { responder(para, MSG_ERRO, (u32)(-e), 0); }

static int vivo(u32 pid)
{
    msg_t m;
    if (sys_tarefa_info(pid, &m) < 0) return 0;
    return m.arg[0] != ESTADO_LIVRE && m.arg[0] != ESTADO_MORTO && m.arg[0] != ESTADO_ZUMBI;
}

static int novo(u32 dono)
{
    for (u32 i = 0; i < MAX_SOCK; i++) {
        if (socks[i].usado) continue;
        sock_t *s = &socks[i];
        mem_zera(s, sizeof *s - sizeof s->buf);          /* o buffer não precisa zerar */
        s->usado = 1;
        s->dono = dono;
        s->par = -1;
        return (int)i;
    }
    return -1;
}

/* Põe até 'max' bytes no buffer de chegada de s; devolve quantos couberam. */
static u32 por(sock_t *s, u32 quem, const char *texto, u32 endereco, u32 max)
{
    u32 cabe = SOCK_BUF - s->n, feito = 0;
    if (max > cabe) max = cabe;
    while (feito < max) {
        u32 pos = (s->ini + s->n) % SOCK_BUF;
        u32 k = SOCK_BUF - pos < max - feito ? SOCK_BUF - pos : max - feito;
        if (endereco) {
            if (sys_copiar_de(quem, s->buf + pos, (u64)endereco + feito, k) < 0) break;
        } else {
            mem_copia(s->buf + pos, texto + feito, k);
        }
        s->n += k;
        feito += k;
    }
    return feito;
}

/* Depois de qualquer mudança em s: atende quem lê e quem espera espaço. */
static void andar(sock_t *s)
{
    if (s->leitor && (s->n || s->fim)) {
        u32 qtd = s->n < s->leitor_max ? s->n : s->leitor_max, feito = 0;
        while (feito < qtd) {
            u32 k = SOCK_BUF - s->ini < qtd - feito ? SOCK_BUF - s->ini : qtd - feito;
            if (sys_copiar_para(s->leitor, s->buf + s->ini, (u64)s->leitor_buf + feito, k) < 0) break;
            s->ini = (s->ini + k) % SOCK_BUF;
            s->n -= k;
            feito += k;
        }
        ok(s->leitor, feito);                            /* 0 = o outro lado fechou */
        s->leitor = 0;
    }
    if (s->escritor && s->n < SOCK_BUF) {                /* abriu espaço */
        u32 feito = por(s, s->escritor, s->escritor_txt, s->escritor_end, s->escritor_tam);
        ok(s->escritor, feito);
        s->escritor = 0;
        if (s->leitor) andar(s);
    }
}

static void fechar(int id)
{
    sock_t *s = &socks[id];
    if (!s->usado) return;
    if (s->par >= 0) {                                   /* o outro lado: fim da conversa */
        sock_t *p = &socks[s->par];
        p->fim = 1;
        p->par = -1;
        andar(p);
        if (p->escritor) { erro(p->escritor, ERRO_TUBO); p->escritor = 0; }
    }
    if (s->escritor) erro(s->escritor, ERRO_TUBO);        /* escrevia para mim */
    if (s->leitor) ok(s->leitor, 0);
    if (s->aceitador) erro(s->aceitador, ERRO_PID);
    for (u32 i = 0; i < s->fila_n; i++) {                /* conexões nunca aceitas */
        sock_t *c = &socks[s->fila[i]];
        c->usado = 0;
        if (c->par >= 0) { socks[c->par].fim = 1; socks[c->par].par = -1; andar(&socks[c->par]); }
    }
    s->usado = 0;
}

/* De vez em quando: os sockets de quem morreu sem fechar. */
static void recolher(void)
{
    for (u32 i = 0; i < MAX_SOCK; i++)
        if (socks[i].usado && !vivo(socks[i].dono)) fechar((int)i);
}

static void pedido(msg_t *m)
{
    u32 quem = m->origem;
    m->texto[MSG_TEXTO_MAX - 1] = '\0';
    if (++pedidos % 16 == 0) recolher();

    if (m->tipo == SOCK_CRIAR || m->tipo == SOCK_PAR) {
        int a = novo(quem);
        if (a < 0) { erro(quem, ERRO_LOTADO); return; }
        if (m->tipo == SOCK_CRIAR) { ok(quem, (u32)a); return; }
        int b = novo(quem);
        if (b < 0) { socks[a].usado = 0; erro(quem, ERRO_LOTADO); return; }
        socks[a].tipo = socks[b].tipo = CONECTADO;
        socks[a].par = b;
        socks[b].par = a;
        responder(quem, MSG_OK, (u32)a, (u32)b);
        return;
    }

    u32 id = ARQ_CANAL(m->arg[0]);
    if (id >= MAX_SOCK || !socks[id].usado) { erro(quem, ERRO_INVALIDO); return; }
    sock_t *s = &socks[id];

    switch (m->tipo) {
    case SOCK_LIGAR:
        if (m->texto[0] == '\0' || s->nome[0]) { erro(quem, ERRO_INVALIDO); return; }
        for (u32 i = 0; i < MAX_SOCK; i++)
            if (socks[i].usado && str_igual(socks[i].nome, m->texto)) { erro(quem, ERRO_EXISTE); return; }
        str_copia(s->nome, m->texto, CAMINHO_MAX);
        ok(quem, 0);
        return;

    case SOCK_OUVIR:
        if (!s->nome[0] || s->tipo == CONECTADO) { erro(quem, ERRO_INVALIDO); return; }
        s->tipo = OUVINDO;
        s->fila_max = m->arg[1] && m->arg[1] < FILA_MAX ? m->arg[1] : FILA_MAX;
        ok(quem, 0);
        return;

    case SOCK_CONECTAR: {
        if (s->tipo != NOVO) { erro(quem, ERRO_INVALIDO); return; }
        int ouv = -1;
        for (u32 i = 0; i < MAX_SOCK; i++)
            if (socks[i].usado && socks[i].tipo == OUVINDO && str_igual(socks[i].nome, m->texto)) ouv = (int)i;
        if (ouv < 0) { erro(quem, ERRO_NAO_ACHADO); return; }   /* ninguém ouvindo: recusada */
        sock_t *o = &socks[ouv];
        if (o->fila_n >= o->fila_max) { erro(quem, ERRO_LOTADO); return; }
        int lado = novo(o->dono);                         /* o lado do servidor */
        if (lado < 0) { erro(quem, ERRO_LOTADO); return; }
        s->tipo = socks[lado].tipo = CONECTADO;
        s->par = lado;
        socks[lado].par = (int)id;
        if (o->aceitador) {                               /* alguém já esperava no accept */
            socks[lado].dono = o->aceitador;
            ok(o->aceitador, (u32)lado);
            o->aceitador = 0;
        } else {
            o->fila[o->fila_n++] = lado;
        }
        ok(quem, 0);
        return;
    }

    case SOCK_ACEITAR:
        if (s->tipo != OUVINDO || s->aceitador) { erro(quem, ERRO_INVALIDO); return; }
        if (s->fila_n) {
            int c = s->fila[0];
            for (u32 i = 1; i < s->fila_n; i++) s->fila[i - 1] = s->fila[i];
            s->fila_n--;
            socks[c].dono = quem;
            ok(quem, (u32)c);
        } else {
            s->aceitador = quem;                          /* responde quando alguém conectar */
        }
        return;

    case SOCK_ESTADO: {
        msg_t r;
        msg_zera(&r);
        r.tipo = MSG_OK;
        r.arg[0] = s->n;
        r.arg[1] = s->fila_n;
        r.arg[2] = (u32)(s->fim || (s->tipo == CONECTADO && s->par < 0));
        r.arg[3] = s->par >= 0 ? SOCK_BUF - socks[s->par].n : 0;
        sys_responder(quem, &r);
        return;
    }

    case MSG_ESCREVER: {
        if (s->tipo != CONECTADO) { erro(quem, ERRO_INVALIDO); return; }
        if (s->par < 0 || !vivo(socks[s->par].dono)) {    /* ninguém do outro lado */
            if (s->par >= 0) fechar(s->par);
            erro(quem, ERRO_TUBO);
            return;
        }
        sock_t *p = &socks[s->par];
        u32 tam = m->arg[3] ? (m->arg[2] < ESCRITA_MAX ? m->arg[2] : ESCRITA_MAX)
                            : (m->arg[2] < MSG_TEXTO_MAX ? m->arg[2] : MSG_TEXTO_MAX - 1);
        if (!tam) { ok(quem, 0); return; }
        if (p->n == SOCK_BUF) {                           /* cheio: espera o outro ler */
            if (p->escritor) { erro(quem, ERRO_OCUPADO); return; }
            p->escritor = quem;
            p->escritor_end = m->arg[3];
            p->escritor_tam = tam;
            if (!m->arg[3]) mem_copia(p->escritor_txt, m->texto, tam);
            return;
        }
        ok(quem, por(p, quem, m->texto, m->arg[3], tam));
        andar(p);
        return;
    }

    case MSG_LER:
        if (s->tipo != CONECTADO && !s->fim) { erro(quem, ERRO_INVALIDO); return; }
        if (s->leitor) { erro(quem, ERRO_OCUPADO); return; }
        s->leitor = quem;
        s->leitor_max = m->arg[1];
        s->leitor_buf = m->arg[2];
        if (!s->n && s->par >= 0 && !vivo(socks[s->par].dono)) fechar(s->par);   /* morreu sem fechar */
        andar(s);
        return;

    case ARQ_FECHAR:
        fechar((int)id);
        ok(quem, 0);
        return;
    }
    erro(quem, ERRO_INVALIDO);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    msg_t m;
    for (;;) {
        sys_receber(PID_QUALQUER, &m);
        pedido(&m);
    }
}
