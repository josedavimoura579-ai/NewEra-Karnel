/*
 * ===========================================================================
 *  NovaEra - usuario/teclado.c   (PID 2, anel 3)
 *  Driver de teclado PS/2 fora do núcleo. O núcleo não sabe o que é uma
 *  tecla: ele só transforma a IRQ 1 numa mensagem MSG_IRQ para este driver.
 *
 *  Cada tecla vira um caractere, que é entregue ao terminal (MSG_TECLA).
 *  Quem decide o que fazer com ela (eco, edição da linha) é o terminal.
 *
 *  Também é o driver do MOUSE PS/2 (o mesmo controlador): os movimentos
 *  vão para o servidor de vídeo (MSG_MOUSE), que desenha a seta.
 *
 *  Protocolo:
 *    MSG_REINICIAR    -> reinicia o computador pela porta 0x64
 *    MSG_DESLIGAR     -> desliga (o ACPI das máquinas virtuais)
 *  Os dois, só para o root.
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

static u8 kb_shift, kb_ctrl, kb_caps, kb_estendido;

static void scancode(u8 sc)
{
    if (sc == 0xE0) { kb_estendido = 1; return; }          /* prefixo (setas...) */
    if (kb_estendido) { kb_estendido = 0; return; }        /* teclas estendidas: ignoradas */

    if (sc & 0x80) {                                       /* tecla SOLTA */
        u8 solta = sc & 0x7F;
        if (solta == 0x2A || solta == 0x36) kb_shift = 0;
        if (solta == 0x1D) kb_ctrl = 0;
        return;
    }

    if (sc == 0x2A || sc == 0x36) { kb_shift = 1; return; }    /* Shift esq/dir */
    if (sc == 0x1D)               { kb_ctrl = 1;  return; }    /* Ctrl esquerdo */
    if (sc == 0x3A)               { kb_caps ^= 1; return; }    /* Caps Lock */
    if (sc >= sizeof(MAPA_NORMAL)) return;

    char base = MAPA_NORMAL[sc];
    char c = kb_shift ? MAPA_SHIFT[sc] : base;
    if (kb_caps && base >= 'a' && base <= 'z')
        c = kb_shift ? base : MAPA_SHIFT[sc];

    /* Ctrl+letra vira o código de controle ASCII: Ctrl+D = 4 ("fim"). */
    if (kb_ctrl && base >= 'a' && base <= 'z') c = (char)(base - 'a' + 1);

    if (c) fila_empurra(c);
}

/* ---------------------------------- Mouse ---------------------------------
 * O mouse PS/2 fala pelo mesmo controlador (o 8042) que o teclado: os
 * bytes dele chegam na mesma porta 0x60, com o bit 5 do estado ligado, e
 * pela IRQ 12. Cada movimento é um pacote de 3 bytes (4 com a roda):
 *   byte 0: botões (bits 0-2), sinais de dx e dy, e o bit 3 sempre 1
 *   byte 1: dx      byte 2: dy (para cima é +)      byte 3: a roda      */

static int  mouse_ok, mouse_roda;          /* existe? tem roda (pacotes de 4)? */
static u8   pacote[4];
static u32  pacote_n;
static long acum_dx, acum_dy, acum_roda;
static u32  botoes;
static int  mudou;

/* ---- O mouse absoluto (vmmouse) das máquinas virtuais ----
 * O QEMU e o VMware sabem onde a seta do computador de fora está (no VNC,
 * onde está o dedo). Pela "porta dos fundos" do VMware, o vmmouse entrega
 * essa posição EXATA (0 a 65535), e a seta do NovaEra fica embaixo do
 * dedo. Ele continua mandando pacotes PS/2, só como aviso de "tem dado". */
void vmware_porta(u32 r[4]);                       /* sys.s */
#define VMW_MAGICO   0x564D5868u
#define VMW_PORTA    0x5658u
#define VMM_DADOS    39u
#define VMM_ESTADO   40u
#define VMM_COMANDO  41u
static int vmmouse;
static u32 abs_x, abs_y;
static int abs_mudou;

static u32 vmw(u32 comando, u32 arg, u32 *b, u32 *c, u32 *d)
{
    u32 r[4] = { VMW_MAGICO, arg, comando, VMW_PORTA };
    vmware_porta(r);
    if (b) *b = r[1];
    if (c) *c = r[2];
    if (d) *d = r[3];
    return r[0];
}

static void vmmouse_iniciar(void)
{
    u32 b;
    if (vmw(10, ~0u, &b, 0, 0) == 0xFFFFFFFFu || b != VMW_MAGICO) return;   /* não é máquina virtual */
    vmw(VMM_COMANDO, 0x45414552u, 0, 0, 0);                 /* "READ_ID" */
    if ((vmw(VMM_ESTADO, 0, 0, 0, 0) & 0xFFFF) == 0) return;
    if (vmw(VMM_DADOS, 1, 0, 0, 0) != 0x3442554Au) return;  /* a versão do vmmouse */
    vmw(VMM_COMANDO, 0x53424152u, 0, 0, 0);                 /* "REQUEST_ABSOLUTE" */
    vmmouse = 1;
}

/* Lê da fila do vmmouse: grupos de 4 números (botões, x, y, roda).
 * Devolve 0 se a fila estava vazia (o movimento veio relativo, pelo PS/2). */
static int vmmouse_ler(void)
{
    int leu = 0;
    for (;;) {
        u32 st = vmw(VMM_ESTADO, 0, 0, 0, 0);
        if ((st & 0xFFFF0000u) == 0xFFFF0000u) { vmmouse = 0; return leu; }   /* erro: volta ao PS/2 */
        if ((st & 0xFFFF) < 4) return leu;
        leu = 1;
        u32 x, y, z;
        u32 bt = vmw(VMM_DADOS, 4, &x, &y, &z);
        abs_x = x & 0xFFFF;
        abs_y = y & 0xFFFF;
        botoes = (bt & 0x20 ? MOUSE_ESQUERDO : 0) | (bt & 0x10 ? MOUSE_DIREITO : 0) |
                 (bt & 0x08 ? MOUSE_MEIO : 0);
        acum_roda -= (long)(signed char)(u8)z;          /* + = para cima */
        abs_mudou = 1;
        mudou = 1;
    }
}

static void ps2_espera_escrever(void)
{
    for (u32 n = 0; n < 100000 && (inb(TECLADO_STATUS) & 0x02); n++) { }
}

static int ps2_le(u8 *b)
{
    for (u32 n = 0; n < 100000; n++)
        if (inb(TECLADO_STATUS) & 0x01) { *b = inb(TECLADO_DADOS); return 1; }
    return 0;
}

/* Um comando para o MOUSE (o 0xD4 diz ao controlador: "o próximo é dele"). */
static int mouse_cmd(u8 c)
{
    ps2_espera_escrever();
    outb(TECLADO_STATUS, 0xD4);
    ps2_espera_escrever();
    outb(TECLADO_DADOS, c);
    u8 r;
    return ps2_le(&r) && r == 0xFA;           /* 0xFA = "entendi" */
}

static void mouse_iniciar(void)
{
    u8 c;
    ps2_espera_escrever();
    outb(TECLADO_STATUS, 0xA8);               /* liga a 2ª porta (a do mouse) */
    ps2_espera_escrever();
    outb(TECLADO_STATUS, 0x20);               /* lê a configuração do controlador */
    if (!ps2_le(&c)) return;
    c |= 0x02;                                /* IRQ 12 ligada */
    c &= (u8)~0x20;                           /* o relógio do mouse ligado */
    ps2_espera_escrever();
    outb(TECLADO_STATUS, 0x60);
    ps2_espera_escrever();
    outb(TECLADO_DADOS, c);
    if (!mouse_cmd(0xF6)) return;             /* o padrão */
    /* a "batida secreta" da roda (IntelliMouse): taxas 200, 100, 80 */
    mouse_cmd(0xF3); mouse_cmd(200);
    mouse_cmd(0xF3); mouse_cmd(100);
    mouse_cmd(0xF3); mouse_cmd(80);
    u8 id = 0;
    if (mouse_cmd(0xF2)) ps2_le(&id);         /* 3 = tem roda */
    mouse_roda = id == 3;
    mouse_ok = mouse_cmd(0xF4);               /* começa a mandar os movimentos */
    if (mouse_ok) vmmouse_iniciar();          /* numa máquina virtual: a posição exata */
}

static void mouse_byte(u8 b)
{
    if (pacote_n == 0 && !(b & 0x08)) return;          /* fora de sincronia: espera um byte 0 */
    pacote[pacote_n++] = b;
    if (pacote_n < (mouse_roda ? 4u : 3u)) return;
    pacote_n = 0;
    if (vmmouse && vmmouse_ler()) return;              /* o pacote só avisou: a posição veio do vmmouse */
    if (pacote[0] & 0xC0) return;                      /* estourou: descarta */
    long dx = pacote[1], dy = pacote[2];
    if (pacote[0] & 0x10) dx -= 256;
    if (pacote[0] & 0x20) dy -= 256;
    acum_dx += dx;
    acum_dy -= dy;                                     /* na tela, para baixo é + */
    if (mouse_roda) acum_roda -= (long)(signed char)(pacote[3] << 4) >> 4;
    botoes = pacote[0] & 7;
    mudou = 1;
}

/* Manda ao servidor de vídeo o que o mouse fez desde a última vez. */
static void entregar_mouse(void)
{
    if (!mudou) return;
    msg_t m;
    msg_zera(&m);
    m.tipo   = MSG_MOUSE;
    m.arg[0] = abs_mudou ? abs_x : (u32)acum_dx;
    m.arg[1] = abs_mudou ? abs_y : (u32)acum_dy;
    m.arg[2] = botoes | (abs_mudou ? MOUSE_ABSOLUTO : 0);
    m.arg[3] = (u32)acum_roda;
    if (sys_enviar(PID_VIDEO, &m) < 0) return;
    acum_dx = acum_dy = acum_roda = 0;
    mudou = abs_mudou = 0;
}

static void reiniciar(void)
{
    for (u32 tentativas = 0x10000; (inb(TECLADO_STATUS) & 0x02) && tentativas; tentativas--)
        ;
    outb(TECLADO_STATUS, 0xFE);            /* comando 0xFE: pulsa a linha RESET da CPU */
}

/* Entrega ao terminal as teclas que estão na fila. Se o terminal estiver
 * reiniciando, a tecla fica na fila para a próxima vez. */
static void entregar_teclas(void)
{
    while (fila_ini != fila_fim) {
        msg_t m;
        msg_zera(&m);
        m.tipo   = MSG_TECLA;
        m.arg[0] = (u8)fila[fila_ini];
        if (sys_enviar(PID_TERMINAL, &m) < 0) return;
        fila_ini = (fila_ini + 1) & (FILA_TAM - 1);
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    for (int i = 0; i < 16 && (inb(TECLADO_STATUS) & 0x01); i++)
        inb(TECLADO_DADOS);                /* esvazia bytes pendentes */
    mouse_iniciar();
    for (int i = 0; i < 16 && (inb(TECLADO_STATUS) & 0x01); i++)
        inb(TECLADO_DADOS);
    sys_irq_assinar(1);
    if (mouse_ok) sys_irq_assinar(12);

    msg_t m;
    for (;;) {
        sys_receber(PID_QUALQUER, &m);

        if (m.origem == PID_KERNEL && m.tipo == MSG_IRQ) {
            u8 st;
            while ((st = inb(TECLADO_STATUS)) & 0x01) {
                u8 b = inb(TECLADO_DADOS);
                if (st & 0x20) mouse_byte(b);  /* bit 5: o byte é do mouse */
                else scancode(b);
            }
            entregar_mouse();
        } else if (m.tipo == MSG_REINICIAR || m.tipo == MSG_DESLIGAR) {
            identidade_t id;
            if (sys_identidade(m.origem, &id) == 0 && id.uid == UID_ROOT) {
                if (m.tipo == MSG_REINICIAR) reiniciar();
                /* "desligar" pelo ACPI: cada máquina virtual tem a sua porta
                 * (QEMU novo, QEMU antigo/Bochs, VirtualBox). Num PC de
                 * verdade seria preciso ler as tabelas ACPI. */
                outw(0x604, 0x2000);
                outw(0xB004, 0x2000);
                outw(0x4004, 0x3400);
            }
        }
        entregar_teclas();
    }
}
