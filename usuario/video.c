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

/* A tela: 80x25 células de 2 bytes (caractere + cor) em USUARIO_VGA. */
static void celula(u32 i, u16 valor) { mem_escreve16(USUARIO_VGA + 2 * i, valor); }
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
        celula(i, (u16)(COR(COR_BRANCO) << 8 | ' '));
    lin = col = 0;
}

static void rolar(void)
{
    for (u32 i = 0; i < COLUNAS * (LINHAS - 1); i++)
        celula(i, mem_le16(USUARIO_VGA + 2 * (i + COLUNAS)));
    for (u32 i = 0; i < COLUNAS; i++)
        celula((LINHAS - 1) * COLUNAS + i, (u16)(COR(COR_BRANCO) << 8 | ' '));
}

static void putc(char c, u8 cor)
{
    if (c == '\n') {
        col = 0;
        lin++;
    } else if (c == '\b') {
        if (col > 0)      col--;
        else if (lin > 0) { lin--; col = COLUNAS - 1; }
        celula(lin * COLUNAS + col, (u16)(cor << 8 | ' '));
    } else {
        celula(lin * COLUNAS + col, (u16)(cor << 8 | (u8)c));
        if (++col >= COLUNAS) { col = 0; lin++; }
    }
    if (lin >= LINHAS) {
        rolar();
        lin = LINHAS - 1;
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
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
