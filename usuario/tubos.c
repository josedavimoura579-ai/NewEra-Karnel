/*
 * ===========================================================================
 *  NovaEra - usuario/tubos.c   (PID 6, anel 3)
 *  Pipes: "ola | cat". Cada tubo é um buffer de 4 KiB com um lado que
 *  escreve e um que lê.
 *
 *  - Escrever num tubo cheio: a resposta só vem quando houver espaço.
 *  - Ler de um tubo vazio: a resposta só vem quando chegar algo, ou com 0
 *    bytes ("acabou") quando não sobrar nenhum escritor.
 *  - Cada ponta conta as referências abertas (TUBO_ABRIR / TUBO_FECHAR);
 *    sem leitores, escrever dá erro; sem ninguém, o tubo é liberado.
 *  O servidor nunca bloqueia: ele simplesmente adia a resposta.
 * ===========================================================================
 */
#include "ulib.h"

#define MAX_TUBOS   16u
#define TUBO_TAM  4096u

typedef struct {
    u32  usado;
    u32  leitores, escritores;              /* referências abertas de cada ponta */
    char buf[TUBO_TAM];
    u32  ini, n;
    u32  leitor, leitor_max, leitor_buffer;      /* leitura pendente (0 = nenhuma) */
    u32  escritor, escritor_n;                   /* escrita pendente (0 = nenhuma) */
    char escritor_dados[MSG_TEXTO_MAX];
} tubo_t;

static tubo_t tubos[MAX_TUBOS];

static void responder(u32 para, u32 tipo, u32 a0)
{
    msg_t r;
    msg_zera(&r);
    r.tipo   = tipo;
    r.arg[0] = a0;
    sys_responder(para, &r);
}

static void por(tubo_t *t, const char *dados, u32 n)
{
    for (u32 i = 0; i < n; i++) t->buf[(t->ini + t->n + i) % TUBO_TAM] = dados[i];
    t->n += n;
}

/* Tenta concluir as operações pendentes depois de qualquer mudança. */
static void andar(tubo_t *t)
{
    if (t->leitor && (t->n || !t->escritores)) {
        char tmp[TUBO_TAM];
        u32 qtd = t->n < t->leitor_max ? t->n : t->leitor_max;
        for (u32 i = 0; i < qtd; i++) tmp[i] = t->buf[(t->ini + i) % TUBO_TAM];
        if (!qtd || sys_copiar_para(t->leitor, tmp, t->leitor_buffer, qtd) >= 0) {
            t->ini = (t->ini + qtd) % TUBO_TAM;
            t->n  -= qtd;
            responder(t->leitor, MSG_OK, qtd);       /* 0 = acabou */
        }
        t->leitor = 0;
    }
    if (t->escritor && !t->leitores) {          /* ninguém mais vai ler */
        responder(t->escritor, MSG_ERRO, (u32)(-ERRO_INVALIDO));
        t->escritor = 0;
    }
    if (t->escritor && TUBO_TAM - t->n >= t->escritor_n) {
        por(t, t->escritor_dados, t->escritor_n);
        responder(t->escritor, MSG_OK, t->escritor_n);
        t->escritor = 0;
        if (t->leitor) andar(t);
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    msg_t m;
    for (;;) {
        sys_receber(PID_QUALQUER, &m);
        u32 quem = m.origem;

        if (m.tipo == TUBO_CRIAR) {
            u32 c = 0;
            while (c < MAX_TUBOS && tubos[c].usado) c++;
            if (c == MAX_TUBOS) { responder(quem, MSG_ERRO, (u32)(-ERRO_LOTADO)); continue; }
            mem_zera(&tubos[c], sizeof(tubos[c]));
            tubos[c].usado      = 1;
            tubos[c].leitores   = 1;
            tubos[c].escritores = 1;
            responder(quem, MSG_OK, c);
            continue;
        }

        u32 c = m.arg[0] & ~CANAL_DONO;
        if (c >= MAX_TUBOS || !tubos[c].usado) {
            responder(quem, MSG_ERRO, (u32)(-ERRO_INVALIDO));
            continue;
        }
        tubo_t *t = &tubos[c];

        switch (m.tipo) {
        case MSG_ESCREVER: {
            u32 n = m.arg[2] < MSG_TEXTO_MAX ? m.arg[2] : MSG_TEXTO_MAX - 1;
            if (!t->leitores || t->escritor) {
                responder(quem, MSG_ERRO, (u32)(-ERRO_INVALIDO));
            } else if (TUBO_TAM - t->n >= n) {
                por(t, m.texto, n);
                responder(quem, MSG_OK, n);
            } else {                                 /* cheio: espera o leitor */
                t->escritor   = quem;
                t->escritor_n = n;
                mem_copia(t->escritor_dados, m.texto, n);
            }
            break;
        }
        case MSG_LER:
            if (t->leitor) { responder(quem, MSG_ERRO, (u32)(-ERRO_INVALIDO)); break; }
            t->leitor        = quem;
            t->leitor_max    = m.arg[1];
            t->leitor_buffer = m.arg[2];
            break;
        case TUBO_ABRIR:
            if (m.arg[1] == TUBO_ESCRITA) t->escritores++; else t->leitores++;
            responder(quem, MSG_OK, 0);
            break;
        case TUBO_FECHAR:
            if (m.arg[1] == TUBO_ESCRITA) { if (t->escritores) t->escritores--; }
            else if (t->leitores) t->leitores--;
            responder(quem, MSG_OK, 0);
            andar(t);
            if (!t->leitores && !t->escritores) t->usado = 0;   /* ninguém mais usa */
            continue;
        default:
            responder(quem, MSG_ERRO, (u32)(-ERRO_INVALIDO));
            continue;
        }
        andar(t);
    }
}
