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
 *  - MSG_ESCREVER de qualquer programa vai para o servidor de vídeo.
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

static char linha[LINHA_MAX];     /* a linha sendo digitada */
static u32  linha_n;

static char fila[FILA_TAM];       /* bytes de linhas já terminadas (FIFO) */
static u32  fila_ini, fila_n;

typedef struct { u32 pid, max, buffer; } leitor_t;
static leitor_t leitores[MAX_LEITORES];   /* quem está esperando uma linha */
static u32      leitores_n;
static u32      fins;                     /* Ctrl+D digitados, ainda não entregues */
static u32      primeiro_plano;           /* grupo que recebe o Ctrl+C (0 = nenhum) */

static void tela(const char *dados, u32 n, u8 cor)
{
    msg_t m;
    msg_zera(&m);
    m.tipo   = MSG_VIDEO_ESCREVER;
    m.arg[0] = cor ? cor : COR(COR_BRANCO);
    for (u32 i = 0; i < n && i < MSG_TEXTO_MAX - 1; i++) m.texto[i] = dados[i];
    sys_enviar(PID_VIDEO, &m);
}

static void responder(u32 para, u32 tipo, u32 a0)
{
    msg_t r;
    msg_zera(&r);
    r.tipo   = tipo;
    r.arg[0] = a0;
    sys_responder(para, &r);
}

/* Entrega linhas prontas a quem está esperando, na ordem de chegada. */
static void atender_leitores(void)
{
    while (leitores_n && (fila_n || fins)) {
        leitor_t l = leitores[0];
        if (!fila_n) {                          /* Ctrl+D: "acabou" */
            fins--;
            responder(l.pid, MSG_OK, 0);
            for (u32 i = 1; i < leitores_n; i++) leitores[i - 1] = leitores[i];
            leitores_n--;
            continue;
        }
        u32 n = 0;
        char buf[LINHA_MAX + 1];
        while (n < fila_n && n < l.max && n < sizeof(buf)) {
            buf[n] = fila[(fila_ini + n) % FILA_TAM];
            if (buf[n++] == '\n') break;        /* uma linha por leitura */
        }
        if (sys_copiar_para(l.pid, buf, l.buffer, n) >= 0) {
            fila_ini = (fila_ini + n) % FILA_TAM;
            fila_n  -= n;
            responder(l.pid, MSG_OK, n);
        }
        /* (se o leitor morreu, a linha fica para o próximo) */
        for (u32 i = 1; i < leitores_n; i++) leitores[i - 1] = leitores[i];
        leitores_n--;
    }
}

/* Ctrl+C: quem estava esperando uma linha acorda com "interrompido" (antes
 * do sinal: assim ninguém fica na lista esperando para sempre) e o grupo
 * do primeiro plano recebe o SIGINT. */
static void interromper(void)
{
    tela("^C\n", 3, COR(COR_CINZA_CLARO));
    linha_n = 0;
    if (!primeiro_plano) {                      /* no prompt do shell: linha vazia */
        fila_n = fila_ini = 0;
        fila[0] = '\n';
        fila_n = 1;
        atender_leitores();
        return;
    }
    fila_n = fila_ini = fins = 0;
    for (u32 i = 0; i < leitores_n; i++)
        responder(leitores[i].pid, MSG_ERRO, (u32)(-ERRO_INTERROMPIDO));
    leitores_n = 0;
    sys_sinal(-(long)primeiro_plano, SINAL_INT);
}

static void tecla(char c)
{
    if (c == 3) {                               /* Ctrl+C */
        interromper();
    } else if (c == 4) {                               /* Ctrl+D */
        if (linha_n == 0) { fins++; tela("^D\n", 3, COR(COR_CINZA_CLARO)); atender_leitores(); }
    } else if (c == '\b') {
        if (linha_n > 0) { linha_n--; tela("\b", 1, 0); }
    } else if (c == '\n') {
        tela("\n", 1, 0);
        for (u32 i = 0; i <= linha_n && fila_n < FILA_TAM; i++) {
            fila[(fila_ini + fila_n) % FILA_TAM] = i < linha_n ? linha[i] : '\n';
            fila_n++;
        }
        linha_n = 0;
        atender_leitores();
    } else if (c >= 32 && c < 127 && linha_n < LINHA_MAX - 1) {
        linha[linha_n++] = c;
        tela(&c, 1, 0);
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    msg_t m;
    for (;;) {
        sys_receber(PID_QUALQUER, &m);

        if (m.tipo == MSG_TECLA && m.origem == PID_TECLADO) {
            tecla((char)m.arg[0]);
        } else if (m.tipo == TERM_GRUPO) {
            primeiro_plano = m.arg[0];
            responder(m.origem, MSG_OK, 0);
        } else if (m.tipo == MSG_ESCREVER) {
            u32 n = m.arg[2] < MSG_TEXTO_MAX ? m.arg[2] : MSG_TEXTO_MAX - 1;
            tela(m.texto, n, (u8)m.arg[1]);
            responder(m.origem, MSG_OK, n);
        } else if (m.tipo == MSG_LER) {
            if (leitores_n == MAX_LEITORES) {
                responder(m.origem, MSG_ERRO, (u32)(-ERRO_LOTADO));
            } else {
                leitores[leitores_n].pid    = m.origem;
                leitores[leitores_n].max    = m.arg[1];
                leitores[leitores_n].buffer = m.arg[2];
                leitores_n++;
                atender_leitores();       /* a resposta vem quando houver linha */
            }
        } else if (m.origem != PID_KERNEL) {
            responder(m.origem, MSG_ERRO, (u32)(-ERRO_INVALIDO));
        }
    }
}
