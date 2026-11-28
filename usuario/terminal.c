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
#define HISTORICO  32u            /* linhas lembradas (a seta para cima) */

typedef struct { u32 pid, max, buffer; } leitor_t;

typedef struct {
    int      usado;
    char     linha[LINHA_MAX];    /* a linha sendo digitada (UTF-8) */
    u32      linha_n;
    u32      pos;                 /* onde está o cursor na linha (em bytes) */
    char     esc[8];              /* uma sequência Esc [ ... chegando */
    u32      esc_n;
    char     hist[HISTORICO][LINHA_MAX];   /* as últimas linhas digitadas */
    u32      hist_n, hist_ver;    /* quantas; qual está sendo vista (hist_n = a linha nova) */
    char     guardada[LINHA_MAX]; /* a linha nova, enquanto se olha o histórico */
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
    t->linha_n = t->pos = 0;
    t->hist_ver = t->hist_n;
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

/* ------------------------- Editar a linha ---------------------------------
 * A linha é UTF-8 (um "ç" ocupa 2 bytes); o cursor anda de letra em letra.
 * Para redesenhar sem apagar a tela, o eco usa as sequências do Linux:
 * Esc [ n D (o cursor n letras para a esquerda), Esc [ n C (para a
 * direita) e Esc [ K (apaga até o fim da linha). */

static int continuacao(char c) { return ((u8)c & 0xC0) == 0x80; }

static u32 letras(const char *s, u32 n)          /* quantas letras em n bytes */
{
    u32 k = 0;
    for (u32 i = 0; i < n; i++) if (!continuacao(s[i])) k++;
    return k;
}

/* Escreve, em pedaços (cada mensagem leva até 60 bytes), se o eco está ligado */
static void eco(tty_t *t, const char *s, u32 n)
{
    if (!t->eco) return;
    while (n) {
        u32 k = n > 60 ? 60 : n;
        while (k < n && k > 1 && continuacao(s[k])) k--;   /* não corta uma letra ao meio */
        tela(t, s, k, 0);
        s += k;
        n -= k;
    }
}

static void mover(tty_t *t, int letras_n)        /* + direita, - esquerda */
{
    if (!letras_n) return;
    char seq[16];
    u32 k = 0, v = (u32)(letras_n < 0 ? -letras_n : letras_n);
    seq[k++] = 27;
    seq[k++] = '[';
    char num[11];
    num_texto(v, num);
    for (u32 i = 0; num[i]; i++) seq[k++] = num[i];
    seq[k++] = letras_n < 0 ? 'D' : 'C';
    eco(t, seq, k);
}

/* Redesenha do cursor até o fim e volta o cursor para o lugar */
static void redesenhar_resto(tty_t *t)
{
    eco(t, t->linha + t->pos, t->linha_n - t->pos);
    eco(t, "\033[K", 3);
    mover(t, -(int)letras(t->linha + t->pos, t->linha_n - t->pos));
}

static void inserir(tty_t *t, const char *b, u32 n)
{
    if (t->linha_n + n >= LINHA_MAX) return;
    for (u32 i = t->linha_n; i > t->pos; i--) t->linha[i - 1 + n] = t->linha[i - 1];
    mem_copia(t->linha + t->pos, b, n);
    t->linha_n += n;
    eco(t, b, n);
    t->pos += n;
    if (t->pos < t->linha_n) redesenhar_resto(t);
}

static void apagar(tty_t *t, u32 de, u32 ate)    /* tira os bytes [de, ate) */
{
    for (u32 i = ate; i < t->linha_n; i++) t->linha[i - (ate - de)] = t->linha[i];
    t->linha_n -= ate - de;
}

static u32 anterior(tty_t *t, u32 p) { do p--; while (p > 0 && continuacao(t->linha[p])); return p; }
static u32 proxima(tty_t *t, u32 p)  { do p++; while (p < t->linha_n && continuacao(t->linha[p])); return p; }

/* Troca a linha inteira (o histórico) */
static void trocar_linha(tty_t *t, const char *nova)
{
    mover(t, -(int)letras(t->linha, t->pos));
    t->linha_n = str_tam(nova);
    mem_copia(t->linha, nova, t->linha_n);
    t->pos = t->linha_n;
    eco(t, t->linha, t->linha_n);
    eco(t, "\033[K", 3);
}

static void historico(tty_t *t, int direcao)
{
    if (direcao < 0 && t->hist_ver == 0) return;
    if (direcao > 0 && t->hist_ver >= t->hist_n) return;
    if (t->hist_ver == t->hist_n) {               /* saindo da linha nova: guarda ela */
        mem_copia(t->guardada, t->linha, t->linha_n);
        t->guardada[t->linha_n] = 0;
    }
    t->hist_ver = (u32)((int)t->hist_ver + direcao);
    trocar_linha(t, t->hist_ver == t->hist_n ? t->guardada : t->hist[t->hist_ver % HISTORICO]);
}

static void lembrar(tty_t *t)
{
    if (!t->eco || !t->linha_n) return;              /* senhas não vão para o histórico */
    t->linha[t->linha_n] = 0;
    if (t->hist_n && str_igual(t->hist[(t->hist_n - 1) % HISTORICO], t->linha)) return;
    str_copia(t->hist[t->hist_n % HISTORICO], t->linha, LINHA_MAX);
    t->hist_n++;
}

/* Uma tecla especial (Esc [ ...) com a linha sendo editada */
static void tecla_especial(tty_t *t, const char *seq)
{
    if (str_igual(seq, "[D") && t->pos > 0) { t->pos = anterior(t, t->pos); mover(t, -1); }
    else if (str_igual(seq, "[C") && t->pos < t->linha_n) { t->pos = proxima(t, t->pos); mover(t, 1); }
    else if (str_igual(seq, "[H") || str_igual(seq, "[1~")) { mover(t, -(int)letras(t->linha, t->pos)); t->pos = 0; }
    else if (str_igual(seq, "[F") || str_igual(seq, "[4~")) {
        mover(t, (int)letras(t->linha + t->pos, t->linha_n - t->pos));
        t->pos = t->linha_n;
    } else if (str_igual(seq, "[3~") && t->pos < t->linha_n) {             /* Delete */
        apagar(t, t->pos, proxima(t, t->pos));
        redesenhar_resto(t);
    } else if (str_igual(seq, "[A")) historico(t, -1);
    else if (str_igual(seq, "[B")) historico(t, 1);
    /* (as outras, como F1, não fazem nada numa linha) */
}

static void enter(tty_t *t)
{
    mover(t, (int)letras(t->linha + t->pos, t->linha_n - t->pos));     /* o cursor vai para o fim */
    tela(t, "\n", 1, 0);
    lembrar(t);
    t->hist_ver = t->hist_n;
    for (u32 i = 0; i <= t->linha_n && t->fila_n < FILA_TAM; i++) {
        t->fila[(t->fila_ini + t->fila_n) % FILA_TAM] = i < t->linha_n ? t->linha[i] : '\n';
        t->fila_n++;
    }
    t->linha_n = t->pos = 0;
    atender_leitores(t);
}

/* As teclas de uma mensagem: um caractere, uma letra UTF-8, ou uma sequência Esc */
static void teclas(tty_t *t, const char *b, u32 n)
{
    if (t->cru) {                               /* vão como vieram (até o Ctrl+C), de uma vez */
        for (u32 i = 0; i < n && t->fila_n < FILA_TAM; i++) t->fila[(t->fila_ini + t->fila_n++) % FILA_TAM] = b[i];
        atender_leitores(t);
        return;
    }
    for (u32 i = 0; i < n; i++) {
        char c = b[i];
        if (t->esc_n) {                         /* no meio de Esc [ ... */
            if (t->esc_n < sizeof t->esc - 1) t->esc[t->esc_n - 1] = c;
            t->esc_n++;
            int fim = (t->esc_n == 2 && c != '[' && c != 'O') || (t->esc_n > 2 && (c >= 0x40 && c <= 0x7E));
            if (fim) {
                t->esc[t->esc_n - 1 < sizeof t->esc ? t->esc_n - 1 : sizeof t->esc - 1] = 0;
                if (t->esc[0] == 'O') t->esc[0] = '[';   /* Esc O H/F = Esc [ H/F */
                tecla_especial(t, t->esc);
                t->esc_n = 0;
            }
            continue;
        }
        if (c == 27) { if (i + 1 < n) t->esc_n = 1; continue; }   /* Esc sozinho: nada */
        if (c == 3) interromper(t);                                 /* Ctrl+C */
        else if (c == 4) {                                          /* Ctrl+D */
            if (t->linha_n == 0) { t->fins++; tela(t, "^D\n", 3, COR(COR_CINZA_CLARO)); atender_leitores(t); }
        } else if (c == '\b' || c == 127) {
            if (t->pos > 0) {
                u32 p = anterior(t, t->pos);
                apagar(t, p, t->pos);
                t->pos = p;
                if (t->eco) { mover(t, -1); redesenhar_resto(t); }
            }
        } else if (c == '\n') enter(t);
        else if ((u8)c >= 0xC0) {                                   /* uma letra UTF-8 inteira */
            u32 k = 1;
            while (i + k < n && continuacao(b[i + k])) k++;
            inserir(t, b + i, k);
            i += k - 1;
        } else if ((u8)c >= 32 && c != 127 && !continuacao(c)) inserir(t, &c, 1);
    }
}

static void tecla(tty_t *t, char c) { teclas(t, &c, 1); }

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
        int no_arg1 = m.tipo == TERM_GRUPO || m.tipo == TERM_ECO || m.tipo == TERM_CRU || m.tipo == TERM_ACORDAR;
        tty_t *t = tty_de(no_arg1 ? m.arg[1] : m.arg[0]);

        if (m.tipo == MSG_TECLA && m.origem == PID_TECLADO) {
            u32 n = m.arg[1] >= 1 && m.arg[1] <= MSG_TEXTO_MAX ? m.arg[1] : 0;
            if (n) teclas(&ttys[0], m.texto, n);
            else tecla(&ttys[0], (char)m.arg[0]);
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
            teclas(t, m.texto, n);
            responder(m.origem, MSG_OK, n);
        } else if (m.tipo == TERM_PTY_FECHAR && t != &ttys[0]) {
            pty_fechar(t);
            responder(m.origem, MSG_OK, 0);
        } else if (m.tipo == TERM_GRUPO) {
            u32 antes = t->primeiro_plano;
            t->primeiro_plano = m.arg[0];
            if (!t->primeiro_plano) { t->cru = 0; t->eco = 1; }   /* o shell retomou: modo normal */
            responder(m.origem, MSG_OK, antes);
        } else if (m.tipo == TERM_ACORDAR) {
            for (u32 i = 0; i < t->leitores_n; i++) {
                if (t->leitores[i].pid != m.arg[0]) continue;
                responder(t->leitores[i].pid, MSG_ERRO, (u32)(-ERRO_INTERROMPIDO));
                for (u32 k = i + 1; k < t->leitores_n; k++) t->leitores[k - 1] = t->leitores[k];
                t->leitores_n--;
                break;
            }
            responder(m.origem, MSG_OK, 0);
        } else if (m.tipo == TERM_CRU) {
            t->cru = m.arg[0] != 0;
            if (t->cru) t->linha_n = t->pos = 0;
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
