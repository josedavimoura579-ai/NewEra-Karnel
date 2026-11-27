/*
 * ===========================================================================
 *  NovaEra - usuario/terminal.c   (PID 5, anel 3)
 *  O terminal (o "TTY"): liga o teclado e a tela aos programas.
 *
 *  - O driver de teclado entrega cada tecla aqui (MSG_TECLA).
 *  - O terminal edita a linha (Backspace), mostra o eco e, no Enter,
 *    entrega a linha inteira a quem pediu com MSG_LER.
 *  - Ctrl+D numa linha vazia é o "fim da entrada": quem lê recebe 0 bytes
 *    (é assim que um "cat" lendo do teclado termina).
 *  - Ctrl+C manda o SIGINT ao grupo do "primeiro plano" (o comando que o
 *    shell está rodando, avisado com TERM_GRUPO) e joga fora a linha e o
 *    que estava digitado adiantado. Sem comando rodando, só pula de linha.
 *  - TERM_ECO 0 esconde o que se digita (para as senhas).
 *  - TERM_CRU 1 é o modo "cru": cada tecla vai na hora para quem lê, sem
 *    eco nem edição, e o Ctrl+C também vai como uma tecla (o servidor de
 *    janelas usa). O modo volta ao normal quando o shell retoma o terminal.
 *  - MSG_ESCREVER de qualquer programa vai para o servidor de vídeo.
 *
 *  E os TERMINAIS VIRTUAIS (os "pty" do Unix): além do console (o canal 0:
 *  o teclado e a tela de verdade), cada canal 1..TERM_PTY_MAX é um terminal
 *  inteiro, com a sua linha, o seu eco e o seu Ctrl+C. Quem o criou (o
 *  programa "term", numa janela) faz o papel do teclado e da tela: manda as
 *  teclas (TERM_PTY_TECLAS) e lê o que os programas escreveram
 *  (TERM_PTY_LER). Os programas nem sabem a diferença.
 *
 *  É a entrada e a saída padrão de todo programa que não foi redirecionado
 *  para um arquivo ou um pipe. No Linux isso mora dentro do kernel; aqui é
 *  só mais um processo.
 * ===========================================================================
 */
#include "ulib.h"

#define LINHA_MAX   200u
#define FILA_TAM   1024u          /* linhas prontas, esperando um leitor */
#define MAX_LEITORES  8u
#define SAIDA_TAM  16384u         /* terminal virtual: o que foi escrito, esperando o dono ler */
#define MAX_ESCRITORES 8u         /* quem escreveu com a saída cheia, esperando espaço */

typedef struct { u32 pid, max, buffer; } leitor_t;

typedef struct {
    int      usado;
    char     linha[LINHA_MAX];    /* a linha sendo digitada */
    u32      linha_n;
    char     fila[FILA_TAM];      /* bytes de linhas já terminadas (FIFO) */
    u32      fila_ini, fila_n;
    leitor_t leitores[MAX_LEITORES];   /* quem está esperando uma linha */
    u32      leitores_n;
    u32      fins;                /* Ctrl+D digitados, ainda não entregues */
    u32      primeiro_plano;      /* grupo que recebe o Ctrl+C (0 = nenhum) */
    int      eco, cru;
    /* só nos terminais virtuais: */
    u32      dono;                /* quem criou (o programa da janela) */
    u8       saida[SAIDA_TAM];    /* o que os programas escreveram (FIFO) */
    u32      saida_ini, saida_n;
    u8       cor;                 /* a última cor posta na saída */
    leitor_t mestre;              /* o dono esperando saída (pid 0 = ninguém) */
    msg_t    escritores[MAX_ESCRITORES];
    u32      escritores_n;
} tty_t;

static tty_t ttys[TERM_PTY_MAX + 1];      /* [0] = o console */

static tty_t *tty_de(u32 canal)
{
    canal &= ~CANAL_DONO;
    if (canal > TERM_PTY_MAX || !ttys[canal].usado) return 0;
    return &ttys[canal];
}

static void responder(u32 para, u32 tipo, u32 a0)
{
    msg_t r;
    msg_zera(&r);
    r.tipo   = tipo;
    r.arg[0] = a0;
    sys_responder(para, &r);
}

static u32 saida_livre(tty_t *t) { return SAIDA_TAM - t->saida_n; }

static void saida_poe(tty_t *t, u8 b)
{
    if (!saida_livre(t)) return;
    t->saida[(t->saida_ini + t->saida_n) % SAIDA_TAM] = b;
    t->saida_n++;
}

/* O dono está esperando? Entrega o que houver. */
static void atender_mestre(tty_t *t)
{
    if (!t->mestre.pid || !t->saida_n) return;
    u8 buf[4096];
    u32 n = 0;
    while (n < t->saida_n && n < t->mestre.max && n < sizeof buf) {
        buf[n] = t->saida[(t->saida_ini + n) % SAIDA_TAM];
        n++;
    }
    if (sys_copiar_para(t->mestre.pid, buf, t->mestre.buffer, n) >= 0) {
        t->saida_ini = (t->saida_ini + n) % SAIDA_TAM;
        t->saida_n -= n;
        responder(t->mestre.pid, MSG_OK, n);
    }
    t->mestre.pid = 0;
}

/* Escreve no terminal: o console vai para o vídeo; um virtual, para a saída */
static void tela(tty_t *t, const char *dados, u32 n, u8 cor)
{
    if (!cor) cor = COR(COR_BRANCO);
    if (t == &ttys[0]) {
        msg_t m;
        msg_zera(&m);
        m.tipo   = MSG_VIDEO_ESCREVER;
        m.arg[0] = cor;
        for (u32 i = 0; i < n && i < MSG_TEXTO_MAX - 1; i++) m.texto[i] = dados[i];
        sys_enviar(PID_VIDEO, &m);
        return;
    }
    if (cor != t->cor && saida_livre(t) >= 3) {
        saida_poe(t, 27);
        saida_poe(t, TERM_COR);
        saida_poe(t, cor);
        t->cor = cor;
    }
    for (u32 i = 0; i < n; i++) saida_poe(t, (u8)dados[i]);
    atender_mestre(t);
}

/* Entrega linhas prontas a quem está esperando, na ordem de chegada. */
static void atender_leitores(tty_t *t)
{
    while (t->leitores_n && (t->fila_n || t->fins)) {
        leitor_t l = t->leitores[0];
        if (!t->fila_n) {                       /* Ctrl+D: "acabou" */
            t->fins--;
            responder(l.pid, MSG_OK, 0);
            for (u32 i = 1; i < t->leitores_n; i++) t->leitores[i - 1] = t->leitores[i];
            t->leitores_n--;
            continue;
        }
        u32 n = 0;
        char buf[LINHA_MAX + 1];
        while (n < t->fila_n && n < l.max && n < sizeof(buf)) {
            buf[n] = t->fila[(t->fila_ini + n) % FILA_TAM];
            if (buf[n++] == '\n') break;        /* uma linha por leitura */
        }
        if (sys_copiar_para(l.pid, buf, l.buffer, n) >= 0) {
            t->fila_ini = (t->fila_ini + n) % FILA_TAM;
            t->fila_n  -= n;
            responder(l.pid, MSG_OK, n);
        }
        /* (se o leitor morreu, a linha fica para o próximo) */
        for (u32 i = 1; i < t->leitores_n; i++) t->leitores[i - 1] = t->leitores[i];
        t->leitores_n--;
    }
}

/* Ctrl+C: quem estava esperando uma linha acorda com "interrompido" (antes
 * do sinal: assim ninguém fica na lista esperando para sempre) e o grupo
 * do primeiro plano recebe o SIGINT. */
static void interromper(tty_t *t)
{
    tela(t, "^C\n", 3, COR(COR_CINZA_CLARO));
    t->linha_n = 0;
    t->eco = 1;
    t->cru = 0;
    if (!t->primeiro_plano) {                   /* no prompt do shell: linha vazia */
        t->fila_n = t->fila_ini = 0;
        t->fila[0] = '\n';
        t->fila_n = 1;
        atender_leitores(t);
        return;
    }
    t->fila_n = t->fila_ini = t->fins = 0;
    for (u32 i = 0; i < t->leitores_n; i++)
        responder(t->leitores[i].pid, MSG_ERRO, (u32)(-ERRO_INTERROMPIDO));
    t->leitores_n = 0;
    sys_sinal(-(long)t->primeiro_plano, SINAL_INT);
}

static void tecla(tty_t *t, char c)
{
    if (t->cru) {                               /* a tecla vai como veio (até o Ctrl+C) */
        if (t->fila_n < FILA_TAM) t->fila[(t->fila_ini + t->fila_n++) % FILA_TAM] = c;
        atender_leitores(t);
    } else if (c == 3) {                        /* Ctrl+C */
        interromper(t);
    } else if (c == 4) {                        /* Ctrl+D */
        if (t->linha_n == 0) { t->fins++; tela(t, "^D\n", 3, COR(COR_CINZA_CLARO)); atender_leitores(t); }
    } else if (c == '\b') {
        if (t->linha_n > 0) { t->linha_n--; if (t->eco) tela(t, "\b", 1, 0); }
    } else if (c == '\n') {
        tela(t, "\n", 1, 0);
        for (u32 i = 0; i <= t->linha_n && t->fila_n < FILA_TAM; i++) {
            t->fila[(t->fila_ini + t->fila_n) % FILA_TAM] = i < t->linha_n ? t->linha[i] : '\n';
            t->fila_n++;
        }
        t->linha_n = 0;
        atender_leitores(t);
    } else if (c >= 32 && c < 127 && t->linha_n < LINHA_MAX - 1) {
        t->linha[t->linha_n++] = c;
        if (t->eco) tela(t, &c, 1, 0);
    }
}

/* MSG_ESCREVER; num terminal virtual com a saída cheia, a resposta espera */
static int escrever(tty_t *t, msg_t *m)
{
    u32 n = m->arg[2] < MSG_TEXTO_MAX ? m->arg[2] : MSG_TEXTO_MAX - 1;
    if (t != &ttys[0] && saida_livre(t) < n + 3) return 0;
    tela(t, m->texto, n, (u8)m->arg[1]);
    responder(m->origem, MSG_OK, n);
    return 1;
}

static void atender_escritores(tty_t *t)
{
    while (t->escritores_n && escrever(t, &t->escritores[0])) {
        for (u32 i = 1; i < t->escritores_n; i++) t->escritores[i - 1] = t->escritores[i];
        t->escritores_n--;
    }
}

static void tty_iniciar(tty_t *t)
{
    mem_zera(t, sizeof *t);
    t->usado = 1;
    t->eco = 1;
}

/* O dono de um terminal virtual morreu sem fechar? */
static int vivo(u32 pid)
{
    msg_t m;
    if (sys_tarefa_info(pid, &m) < 0) return 0;
    return m.arg[0] != ESTADO_LIVRE && m.arg[0] != ESTADO_MORTO && m.arg[0] != ESTADO_ZUMBI;
}

static void pty_fechar(tty_t *t)
{
    for (u32 i = 0; i < t->leitores_n; i++) responder(t->leitores[i].pid, MSG_OK, 0);   /* fim da entrada */
    for (u32 i = 0; i < t->escritores_n; i++) responder(t->escritores[i].origem, MSG_ERRO, (u32)(-ERRO_TUBO));
    if (t->mestre.pid) responder(t->mestre.pid, MSG_OK, 0);
    if (t->primeiro_plano) sys_sinal(-(long)t->primeiro_plano, SINAL_HUP);
    t->usado = 0;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    tty_iniciar(&ttys[0]);

    msg_t m;
    for (;;) {
        sys_receber(PID_QUALQUER, &m);
        tty_t *t = tty_de(m.tipo == TERM_GRUPO || m.tipo == TERM_ECO || m.tipo == TERM_CRU ? m.arg[1] : m.arg[0]);

        if (m.tipo == MSG_TECLA && m.origem == PID_TECLADO) {
            tecla(&ttys[0], (char)m.arg[0]);
        } else if (m.tipo == TERM_PTY_CRIAR) {
            u32 c = 0;
            for (u32 i = 1; i <= TERM_PTY_MAX && !c; i++) {
                if (ttys[i].usado && !vivo(ttys[i].dono)) pty_fechar(&ttys[i]);   /* sobrou de alguém */
                if (!ttys[i].usado) c = i;
            }
            if (!c) { responder(m.origem, MSG_ERRO, (u32)(-ERRO_LOTADO)); continue; }
            tty_iniciar(&ttys[c]);
            ttys[c].dono = m.origem;
            responder(m.origem, MSG_OK, c);
        } else if (!t) {
            if (m.origem != PID_KERNEL) responder(m.origem, MSG_ERRO, (u32)(-ERRO_INVALIDO));
        } else if (m.tipo == TERM_PTY_LER && t != &ttys[0]) {
            if (t->mestre.pid) { responder(m.origem, MSG_ERRO, (u32)(-ERRO_LOTADO)); continue; }
            t->mestre.pid = m.origem;
            t->mestre.max = m.arg[1];
            t->mestre.buffer = m.arg[2];
            atender_mestre(t);
            atender_escritores(t);
        } else if (m.tipo == TERM_PTY_TECLAS && t != &ttys[0]) {
            u32 n = m.arg[2] < MSG_TEXTO_MAX ? m.arg[2] : MSG_TEXTO_MAX;
            for (u32 i = 0; i < n; i++) tecla(t, m.texto[i]);
            responder(m.origem, MSG_OK, n);
        } else if (m.tipo == TERM_PTY_FECHAR && t != &ttys[0]) {
            pty_fechar(t);
            responder(m.origem, MSG_OK, 0);
        } else if (m.tipo == TERM_GRUPO) {
            u32 antes = t->primeiro_plano;
            t->primeiro_plano = m.arg[0];
            if (!t->primeiro_plano) { t->cru = 0; t->eco = 1; }   /* o shell retomou: modo normal */
            responder(m.origem, MSG_OK, antes);
        } else if (m.tipo == TERM_CRU) {
            t->cru = m.arg[0] != 0;
            if (t->cru) t->linha_n = 0;
            responder(m.origem, MSG_OK, 0);
        } else if (m.tipo == TERM_ECO) {
            t->eco = m.arg[0] != 0;
            responder(m.origem, MSG_OK, 0);
        } else if (m.tipo == MSG_ESCREVER) {
            if (!escrever(t, &m)) {
                if (t->escritores_n == MAX_ESCRITORES) responder(m.origem, MSG_ERRO, (u32)(-ERRO_LOTADO));
                else t->escritores[t->escritores_n++] = m;     /* a resposta vem quando o dono ler */
            }
        } else if (m.tipo == MSG_LER) {
            if (t->leitores_n == MAX_LEITORES) {
                responder(m.origem, MSG_ERRO, (u32)(-ERRO_LOTADO));
            } else {
                t->leitores[t->leitores_n].pid    = m.origem;
                t->leitores[t->leitores_n].max    = m.arg[1];
                t->leitores[t->leitores_n].buffer = m.arg[2];
                t->leitores_n++;
                atender_leitores(t);      /* a resposta vem quando houver linha */
            }
        } else if (m.origem != PID_KERNEL) {
            responder(m.origem, MSG_ERRO, (u32)(-ERRO_INVALIDO));
        }
    }
}
