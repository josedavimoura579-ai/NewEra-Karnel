/*
 * ===========================================================================
 *  NovaEra - usuario/teclado.c   (PID 2, anel 3)
 *  Driver de teclado PS/2 fora do núcleo. O núcleo não sabe o que é uma
 *  tecla: ele só transforma a IRQ 1 numa mensagem MSG_IRQ para este driver.
 *
 *  Protocolo:
 *    MSG_TECLADO_LER  -> responde com arg[0] = caractere (espera se preciso)
 *    MSG_REINICIAR    -> reinicia o computador pela porta 0x64
 * ===========================================================================
 */
#include "ulib.h"

#define TECLADO_DADOS  0x60
#define TECLADO_STATUS 0x64

/* Scancodes "set 1" -> ASCII, layout US. Índice = scancode de tecla pressionada. */
static const char MAPA_NORMAL[58] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' '
};

static const char MAPA_SHIFT[58] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' '
};

/* Fila circular: as IRQs produzem caracteres, os pedidos de leitura consomem. */
#define FILA_TAM 256u
static char fila[FILA_TAM];
static u32  fila_ini, fila_fim;

static void fila_empurra(char c)
{
    u32 prox = (fila_fim + 1) & (FILA_TAM - 1);
    if (prox == fila_ini) return;          /* fila cheia: descarta a tecla */
    fila[fila_fim] = c;
    fila_fim = prox;
}

static u8 kb_shift, kb_caps, kb_estendido;

static void scancode(u8 sc)
{
    if (sc == 0xE0) { kb_estendido = 1; return; }          /* prefixo (setas...) */
    if (kb_estendido) { kb_estendido = 0; return; }        /* teclas estendidas: ignoradas */

    if (sc & 0x80) {                                       /* tecla SOLTA */
        u8 solta = sc & 0x7F;
        if (solta == 0x2A || solta == 0x36) kb_shift = 0;
        return;
    }

    if (sc == 0x2A || sc == 0x36) { kb_shift = 1; return; }    /* Shift esq/dir */
    if (sc == 0x3A)               { kb_caps ^= 1; return; }    /* Caps Lock */
    if (sc >= sizeof(MAPA_NORMAL)) return;

    char base = MAPA_NORMAL[sc];
    char c = kb_shift ? MAPA_SHIFT[sc] : base;
    if (kb_caps && base >= 'a' && base <= 'z')
        c = kb_shift ? base : MAPA_SHIFT[sc];

    if (c) fila_empurra(c);
}

static void reiniciar(void)
{
    for (u32 tentativas = 0x10000; (inb(TECLADO_STATUS) & 0x02) && tentativas; tentativas--)
        ;
    outb(TECLADO_STATUS, 0xFE);            /* comando 0xFE: pulsa a linha RESET da CPU */
}

/* Quem está esperando uma tecla (0 = ninguém). */
static u32 leitor;

static void atender_leitor(void)
{
    if (!leitor || fila_ini == fila_fim) return;

    msg_t r;
    msg_zera(&r);
    r.tipo   = MSG_OK;
    r.arg[0] = (u8)fila[fila_ini];
    /* Se o leitor desistiu (morreu, foi reiniciado), a tecla fica na fila. */
    if (sys_responder(leitor, &r) == 0)
        fila_ini = (fila_ini + 1) & (FILA_TAM - 1);
    leitor = 0;
}

void main(u32 reinicios, u32 falha)
{
    (void)reinicios; (void)falha;

    for (int i = 0; i < 16 && (inb(TECLADO_STATUS) & 0x01); i++)
        inb(TECLADO_DADOS);                /* esvazia bytes pendentes */
    sys_irq_assinar(1);

    msg_t m;
    for (;;) {
        sys_receber(PID_QUALQUER, &m);

        if (m.origem == PID_KERNEL && m.tipo == MSG_IRQ) {
            while (inb(TECLADO_STATUS) & 0x01)
                scancode(inb(TECLADO_DADOS));
        } else if (m.tipo == MSG_TECLADO_LER) {
            leitor = m.origem;
        } else if (m.tipo == MSG_REINICIAR) {
            reiniciar();
        }
        atender_leitor();
    }
}
