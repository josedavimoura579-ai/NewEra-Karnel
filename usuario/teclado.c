/*
 * ===========================================================================
 *  NovaEra - usuario/teclado.c   (PID 2, anel 3)
 *  Driver de teclado PS/2 fora do núcleo. O núcleo não sabe o que é uma
 *  tecla: ele só transforma a IRQ 1 numa mensagem MSG_IRQ para este driver.
 *
 *  Cada tecla vira um caractere (ou alguns bytes: os acentos em UTF-8, as
 *  setas como no Linux), entregue ao terminal (MSG_TECLA). Quem decide o
 *  que fazer com ela (eco, edição da linha) é o terminal.
 *
 *  Também é o driver do MOUSE PS/2 (o mesmo controlador): os movimentos
 *  vão para o servidor de vídeo (MSG_MOUSE), que desenha a seta.
 *
 *  Protocolo:
 *    MSG_REINICIAR    -> reinicia o computador pela porta 0x64
 *    MSG_DESLIGAR     -> desliga (o ACPI das máquinas virtuais)
 *  Os dois, só para o root.
 *    TECLADO_MAPA     -> troca o layout: arg[0] = 0 US, 1 ABNT2 (2 = só pergunta)
 * ===========================================================================
 */
#include "ulib.h"

#define TECLADO_DADOS  0x60
#define TECLADO_STATUS 0x64

/* ------------------------------- O teclado --------------------------------
 * Cada tecla apertada chega como um "scancode" (o número da tecla, não a
 * letra). Quem diz que letra é cada tecla é o LAYOUT: no ABNT2 (o teclado
 * brasileiro, o padrão), a tecla ao lado do L é o ç; no US, é o ;.
 *
 * O que sai daqui para o terminal, numa mensagem por tecla (MSG_TECLA,
 * texto = os bytes, arg[1] = quantos):
 *   - letras e acentos em UTF-8 ("ç" = 0xC3 0xA7);
 *   - as teclas especiais como no Linux (os terminais "xterm"):
 *       setas  Esc [ A/B/C/D     Home/End  Esc [ H / Esc [ F
 *       Insert Esc [ 2 ~   Delete Esc [ 3 ~   Page Up/Down  Esc [ 5 ~ / 6 ~
 *       F1-F4  Esc O P..S   F5-F12  Esc [ 15 ~ ... Esc [ 24 ~
 *   - Alt+tecla: Esc e a tecla.
 *
 * As TECLAS MORTAS (´ ` ~ ^ ¨) não escrevem nada sozinhas: esperam a
 * próxima tecla. ´ e depois a = á; ´ e depois espaço = o próprio ´.
 */

/* As teclas mortas (valores fora do Unicode usado aqui) */
#define M_AGUDO  0xE001
#define M_GRAVE  0xE002
#define M_TIL    0xE003
#define M_CIRC   0xE004
#define M_TREMA  0xE005

#define N_TECLAS 0x80
typedef struct { u16 normal[N_TECLAS], shift[N_TECLAS], altgr[N_TECLAS]; } layout_t;

/* Layout US */
static const char US_N[0x3A] =
    "\0\033" "1234567890-=\b"
    "\tqwertyuiop[]\n"
    "\0asdfghjkl;'`"
    "\0\\zxcvbnm,./\0"
    "*\0 ";
static const char US_S[0x3A] =
    "\0\033" "!@#$%^&*()_+\b"
    "\tQWERTYUIOP{}\n"
    "\0ASDFGHJKL:\"~"
    "\0|ZXCVBNM<>?\0"
    "*\0 ";

static layout_t mapa;             /* o layout em uso */
static int mapa_br = 1;

static void montar_layout(int br)
{
    mem_zera(&mapa, sizeof mapa);
    for (u32 i = 0; i < 0x3A; i++) {
        mapa.normal[i] = (u8)US_N[i];
        mapa.shift[i] = (u8)US_S[i];
    }
    if (br) {                                   /* ABNT2: o que muda em relação ao US */
        mapa.shift[0x07] = M_TREMA;             /* Shift+6 = ¨ */
        mapa.normal[0x1A] = M_AGUDO;  mapa.shift[0x1A] = M_GRAVE;
        mapa.normal[0x1B] = '[';      mapa.shift[0x1B] = '{';
        mapa.normal[0x27] = 0xE7;     mapa.shift[0x27] = 0xC7;     /* ç Ç */
        mapa.normal[0x28] = M_TIL;    mapa.shift[0x28] = M_CIRC;
        mapa.normal[0x29] = '\'';     mapa.shift[0x29] = '"';
        mapa.normal[0x2B] = ']';      mapa.shift[0x2B] = '}';
        mapa.normal[0x35] = ';';      mapa.shift[0x35] = ':';
        mapa.normal[0x56] = '\\';     mapa.shift[0x56] = '|';      /* a tecla ao lado do Shift esquerdo */
        mapa.normal[0x73] = '/';      mapa.shift[0x73] = '?';      /* a tecla ao lado do Shift direito */
        mapa.altgr[0x02] = 0xB9; mapa.altgr[0x03] = 0xB2; mapa.altgr[0x04] = 0xB3;   /* ¹ ² ³ */
        mapa.altgr[0x05] = 0xA3; mapa.altgr[0x06] = 0xA2; mapa.altgr[0x07] = 0xAC;   /* £ ¢ ¬ */
        mapa.altgr[0x0D] = 0xA7;                                                    /* § */
        mapa.altgr[0x10] = '/';  mapa.altgr[0x11] = '?'; mapa.altgr[0x12] = 0xB0;   /* / ? ° */
        mapa.altgr[0x1B] = 0xAA; mapa.altgr[0x2B] = 0xBA;                           /* ª º */
    } else {
        mapa.normal[0x56] = '\\'; mapa.shift[0x56] = '|';
    }
    /* o teclado numérico (com o Num Lock ligado) */
    static const char kp[] = "789-456+1230";
    for (u32 i = 0; i < 12; i++) mapa.normal[0x47 + i] = mapa.shift[0x47 + i] = (u8)kp[i];
    mapa.normal[0x53] = mapa.shift[0x53] = br ? ',' : '.';
    mapa.normal[0x37] = mapa.shift[0x37] = '*';
    mapa_br = br;
}

/* Fila de teclas: cada uma, os bytes que ela produz */
#define FILA_TAM 64u
typedef struct { u8 n; char b[12]; } evento_t;
static evento_t fila[FILA_TAM];
static u32  fila_ini, fila_fim;
static evento_t atual;            /* a tecla sendo montada */

static void poe_byte(char c) { if (atual.n < sizeof atual.b) atual.b[atual.n++] = c; }
static void poe_texto(const char *t) { while (*t) poe_byte(*t++); }

static void poe_unicode(u32 u)
{
    if (u < 0x80) poe_byte((char)u);
    else if (u < 0x800) { poe_byte((char)(0xC0 | u >> 6)); poe_byte((char)(0x80 | (u & 63))); }
    else { poe_byte((char)(0xE0 | u >> 12)); poe_byte((char)(0x80 | ((u >> 6) & 63))); poe_byte((char)(0x80 | (u & 63))); }
}

static void fila_empurra(void)
{
    if (!atual.n) return;
    u32 prox = (fila_fim + 1) & (FILA_TAM - 1);
    if (prox != fila_ini) {               /* (fila cheia: descarta a tecla) */
        fila[fila_fim] = atual;
        fila_fim = prox;
    }
    atual.n = 0;
}

static u8 kb_shift, kb_ctrl, kb_caps, kb_alt, kb_altgr, kb_estendido;
static u16 morta;                         /* a tecla morta esperando (0 = nenhuma) */

/* O acento sozinho (a tecla morta e depois espaço) */
static u32 acento(u16 m)
{
    return m == M_AGUDO ? 0xB4 : m == M_GRAVE ? '`' : m == M_TIL ? '~' : m == M_CIRC ? '^' : 0xA8;
}

/* A letra com o acento (0 se não combina) */
static u32 compor(u16 m, u32 c)
{
    static const char base[] = "aeiouAEIOU";
    static const u16 agudo[] = { 0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xC1, 0xC9, 0xCD, 0xD3, 0xDA };
    static const u16 grave[] = { 0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0xC0, 0xC8, 0xCC, 0xD2, 0xD9 };
    static const u16 circ[]  = { 0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0xC2, 0xCA, 0xCE, 0xD4, 0xDB };
    static const u16 trema[] = { 0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0xC4, 0xCB, 0xCF, 0xD6, 0xDC };
    for (u32 i = 0; i < 10; i++) {
        if ((u32)(u8)base[i] != c) continue;
        if (m == M_AGUDO) return agudo[i];
        if (m == M_GRAVE) return grave[i];
        if (m == M_CIRC) return circ[i];
        if (m == M_TREMA) return trema[i];
    }
    if (m == M_TIL) {
        if (c == 'a') return 0xE3;
        if (c == 'o') return 0xF5;
        if (c == 'n') return 0xF1;
        if (c == 'A') return 0xC3;
        if (c == 'O') return 0xD5;
        if (c == 'N') return 0xD1;
    }
    if (m == M_AGUDO && c == 'c') return 0xE7;      /* ´ c = ç (como no Linux) */
    if (m == M_AGUDO && c == 'C') return 0xC7;
    return 0;
}

/* Uma tecla especial (com o prefixo 0xE0, ou F1-F12) */
static void especial(u8 sc, int estendida)
{
    const char *t = 0;
    if (estendida) {
        switch (sc) {
        case 0x48: t = "\033[A"; break;
        case 0x50: t = "\033[B"; break;
        case 0x4D: t = "\033[C"; break;
        case 0x4B: t = "\033[D"; break;
        case 0x47: t = "\033[H"; break;
        case 0x4F: t = "\033[F"; break;
        case 0x52: t = "\033[2~"; break;
        case 0x53: t = "\033[3~"; break;
        case 0x49: t = "\033[5~"; break;
        case 0x51: t = "\033[6~"; break;
        case 0x1C: t = "\n"; break;                /* o Enter do teclado numérico */
        case 0x35: t = "/"; break;                 /* a barra do teclado numérico */
        }
    } else {
        static const char *const f[] = { "\033OP", "\033OQ", "\033OR", "\033OS", "\033[15~", "\033[17~",
                                         "\033[18~", "\033[19~", "\033[20~", "\033[21~" };
        if (sc >= 0x3B && sc <= 0x44) t = f[sc - 0x3B];
        else if (sc == 0x57) t = "\033[23~";
        else if (sc == 0x58) t = "\033[24~";
    }
    if (t) { poe_texto(t); fila_empurra(); }
}

static void scancode(u8 sc)
{
    if (sc == 0xE0) { kb_estendido = 1; return; }          /* prefixo (setas...) */
    int estendida = kb_estendido;
    kb_estendido = 0;

    if (sc & 0x80) {                                       /* tecla SOLTA */
        u8 solta = sc & 0x7F;
        if (!estendida && (solta == 0x2A || solta == 0x36)) kb_shift = 0;
        if (solta == 0x1D) kb_ctrl = 0;
        if (solta == 0x38) { if (estendida) kb_altgr = 0; else kb_alt = 0; }
        return;
    }
    if (estendida) {
        if (sc == 0x38) { kb_altgr = 1; return; }          /* AltGr (o Alt da direita) */
        if (sc == 0x1D) { kb_ctrl = 1; return; }
        if (sc == 0x2A || sc == 0x36 || sc == 0x5B || sc == 0x5C || sc == 0x5D) return;   /* Shift falso, Windows */
        especial(sc, 1);
        return;
    }
    if (sc == 0x2A || sc == 0x36) { kb_shift = 1; return; }    /* Shift esq/dir */
    if (sc == 0x1D)               { kb_ctrl = 1;  return; }    /* Ctrl esquerdo */
    if (sc == 0x38)               { kb_alt = 1;   return; }    /* Alt */
    if (sc == 0x3A)               { kb_caps ^= 1; return; }    /* Caps Lock */
    if ((sc >= 0x3B && sc <= 0x44) || sc == 0x57 || sc == 0x58) { especial(sc, 0); return; }
    if (sc >= N_TECLAS) return;

    u32 base = mapa.normal[sc];
    u32 c = kb_altgr && mapa.altgr[sc] ? mapa.altgr[sc] : kb_shift ? mapa.shift[sc] : base;
    int letra = (base >= 'a' && base <= 'z') || base == 0xE7;
    if (kb_caps && letra && !kb_altgr) c = kb_shift ? base : mapa.shift[sc];

    /* Ctrl+letra vira o código de controle ASCII: Ctrl+D = 4 ("fim"). */
    if (kb_ctrl && base >= 'a' && base <= 'z') c = base - 'a' + 1;
    if (!c) return;

    if (c >= M_AGUDO && c <= M_TREMA) {                    /* uma tecla morta */
        if (morta) {                                       /* duas seguidas: os dois acentos */
            poe_unicode(acento(morta));
            if (morta == c) { morta = 0; fila_empurra(); return; }
            fila_empurra();
        }
        morta = (u16)c;
        return;
    }
    if (morta) {
        u32 composta = compor(morta, c);
        if (composta) c = composta;
        else if (c == ' ') c = acento(morta);
        else poe_unicode(acento(morta));                   /* não combina: o acento e a letra */
        morta = 0;
    }
    if (kb_alt && c < 0x80 && c >= ' ') poe_byte('\033');  /* Alt+tecla = Esc e a tecla */
    poe_unicode(c);
    fila_empurra();
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

/* Entrega ao terminal as teclas que estão na fila (uma mensagem por tecla).
 * Se o terminal estiver reiniciando, a tecla fica na fila para a próxima vez. */
static void entregar_teclas(void)
{
    while (fila_ini != fila_fim) {
        msg_t m;
        msg_zera(&m);
        m.tipo   = MSG_TECLA;
        evento_t *e = &fila[fila_ini];
        m.arg[0] = (u8)e->b[0];
        m.arg[1] = e->n;
        mem_copia(m.texto, e->b, e->n);
        if (sys_enviar(PID_TERMINAL, &m) < 0) return;
        fila_ini = (fila_ini + 1) & (FILA_TAM - 1);
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    montar_layout(1);                      /* o ABNT2 (o /etc/teclado pode trocar: TECLADO_MAPA) */
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
        } else if (m.tipo == TECLADO_MAPA) {            /* qualquer um troca o layout (como o setxkbmap) */
            if (m.arg[0] <= 1) montar_layout((int)m.arg[0]);
            msg_t r;
            msg_zera(&r);
            r.tipo = MSG_OK;
            r.arg[0] = (u32)mapa_br;
            sys_responder(m.origem, &r);
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
