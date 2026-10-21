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

/* ------------------------- Acentos: UTF-8 -> CP437 ------------------------
 * Os textos em C e nos arquivos são UTF-8 ("ç" = 0xC3 0xA7); a placa VGA
 * usa a tabela CP437 ("ç" = 0x87). Uma sequência UTF-8 válida é
 * convertida; um byte solto acima de 127 já é CP437 e passa direto.     */

static u8 cp437_latin(u32 cp)
{
    switch (cp) {
    case 0xE1: return 0xA0; case 0xE0: return 0x85; case 0xE2: return 0x83; case 0xE3: return 'a';
    case 0xE7: return 0x87; case 0xE9: return 0x82; case 0xEA: return 0x88; case 0xED: return 0xA1;
    case 0xF3: return 0xA2; case 0xF4: return 0x93; case 0xF5: return 'o';  case 0xFA: return 0xA3;
    case 0xFC: return 0x81; case 0xC7: return 0x80; case 0xC9: return 0x90; case 0xBA: return 0xA7;
    case 0xAA: return 0xA6; case 0xB0: return 0xF8;
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: return 'A';
    case 0xCA: return 'E';  case 0xCD: return 'I';
    case 0xD3: case 0xD4: case 0xD5: return 'O';
    case 0xDA: return 'U';
    }
    return '?';
}

static u8  utf8_lider;        /* 1º byte de uma sequência em andamento */
static u32 utf8_resta;        /* quantos bytes de continuação faltam */
static u32 utf8_valor;

static void escreve_byte(u8 b, u8 cor)
{
    if (utf8_resta) {
        if ((b & 0xC0) == 0x80) {                    /* continuação */
            utf8_valor = (utf8_valor << 6) | (b & 0x3F);
            if (--utf8_resta == 0) putc((char)cp437_latin(utf8_valor), cor);
            return;
        }
        putc((char)utf8_lider, cor);                 /* não era UTF-8: era CP437 */
        utf8_resta = 0;
    }
    if (b >= 0xC2 && b <= 0xDF)      { utf8_lider = b; utf8_resta = 1; utf8_valor = b & 0x1F; }
    else if (b >= 0xE0 && b <= 0xEF) { utf8_lider = b; utf8_resta = 2; utf8_valor = b & 0x0F; }
    else if (b >= 0xF0 && b <= 0xF4) { utf8_lider = b; utf8_resta = 3; utf8_valor = b & 0x07; }
    else putc((char)b, cor);
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
                escreve_byte((u8)m.texto[i], (u8)m.arg[0]);
        } else if (m.tipo == MSG_VIDEO_LIMPAR) {
            limpar();
        }
        cursor_mover();
    }
}
