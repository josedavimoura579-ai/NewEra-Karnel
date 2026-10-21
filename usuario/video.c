/*
 * ===========================================================================
 *  NovaEra - usuario/video.c   (PID 1, anel 3)
 *  Servidor de vídeo: o ÚNICO processo com a memória VGA mapeada.
 *  Quem quiser escrever na tela manda uma mensagem para ele.
 * ===========================================================================
 */
#include "ulib.h"

#define COLUNAS 80u
#define LINHAS  25u

static volatile u16 *const tela = (volatile u16 *)USUARIO_VGA;
static u32 lin, col;

static void cursor_mover(void)
{
    u16 pos = (u16)(lin * COLUNAS + col);
    outb(0x3D4, 0x0F); outb(0x3D5, (u8)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (u8)(pos >> 8));
}

/* Começa de onde o núcleo parou de escrever durante o boot. */
static void cursor_ler(void)
{
    outb(0x3D4, 0x0F); u32 pos = inb(0x3D5);
    outb(0x3D4, 0x0E); pos |= (u32)inb(0x3D5) << 8;
    lin = pos / COLUNAS;
    col = pos % COLUNAS;
}

static void limpar(void)
{
    for (u32 i = 0; i < COLUNAS * LINHAS; i++)
        tela[i] = (u16)(COR(COR_BRANCO) << 8 | ' ');
    lin = col = 0;
}

static void rolar(void)
{
    for (u32 i = 0; i < COLUNAS * (LINHAS - 1); i++)
        tela[i] = tela[i + COLUNAS];
    for (u32 i = 0; i < COLUNAS; i++)
        tela[(LINHAS - 1) * COLUNAS + i] = (u16)(COR(COR_BRANCO) << 8 | ' ');
}

static void putc(char c, u8 cor)
{
    if (c == '\n') {
        col = 0;
        lin++;
    } else if (c == '\b') {
        if (col > 0)      col--;
        else if (lin > 0) { lin--; col = COLUNAS - 1; }
        tela[lin * COLUNAS + col] = (u16)(cor << 8 | ' ');
    } else {
        tela[lin * COLUNAS + col] = (u16)(cor << 8 | (u8)c);
        if (++col >= COLUNAS) { col = 0; lin++; }
    }
    if (lin >= LINHAS) {
        rolar();
        lin = LINHAS - 1;
    }
}

void main(u32 reinicios, u32 falha)
{
    (void)reinicios; (void)falha;
    cursor_ler();

    msg_t m;
    for (;;) {
        sys_receber(PID_QUALQUER, &m);

        if (m.tipo == MSG_VIDEO_ESCREVER) {
            for (u32 i = 0; i < MSG_TEXTO_MAX && m.texto[i]; i++)
                putc(m.texto[i], (u8)m.arg[0]);
        } else if (m.tipo == MSG_VIDEO_LIMPAR) {
            limpar();
        }
        cursor_mover();
    }
}
