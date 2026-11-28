/*
 * ===========================================================================
 *  NovaEra - usuario/video.c   (PID 1, anel 3)
 *  Servidor de vídeo: o ÚNICO processo com a memória da tela mapeada.
 *  Quem quiser escrever na tela manda uma mensagem para ele.
 *
 *  Dois modos:
 *   - TEXTO: a placa VGA de sempre, 80 × 25 letras (a placa desenha as
 *     letras sozinha). É o padrão.
 *   - GRÁFICO (com "grafico" na linha de boot): a placa BGA (QEMU -vga std,
 *     VirtualBox, Bochs) em 1024 × 768 pontos, 32 bits cada. Aqui quem
 *     desenha cada letra é este servidor, ponto por ponto, com a fonte
 *     8 × 16 de fonte8x16.h: 128 × 43 letras, com a logo do NovaEra no topo.
 *  Tudo o que vai para a tela também sai pela porta serial COM1, sem
 *  conversão: no QEMU, "-serial stdio" mostra a saída no terminal de fora
 *  (útil para testes e para guardar o que um programa escreveu).
 * ===========================================================================
 */
#include "ulib.h"
#include "fonte8x16.h"

#define COLUNAS 80u
#define LINHAS  25u
static u32 lin, col;                        /* o cursor do modo texto */

/* --------------------------- O modo gráfico ------------------------------ */

#define LARG       1024u
#define ALT        768u
#define TOPO       80u                      /* a faixa da logo, em cima */
#define G_COLUNAS  (LARG / FONTE_LARGURA)   /* 128 */
#define G_LINHAS   ((ALT - TOPO) / FONTE_ALTURA)   /* 43 */

static int grafico;                         /* 1 = a tela é a BGA */
static u32 *tela;                           /* os pontos: 0x00RRGGBB */
static u32 g_lin, g_col;

/* O texto do terminal, guardado: para redesenhar depois de uma tela cheia. */
static u16 g_letras[G_LINHAS][G_COLUNAS];
static u8  g_cores[G_LINHAS][G_COLUNAS];

/* A tela cheia de um programa (VIDEO_TELA_ABRIR): enquanto ela estiver
 * aberta, o texto vai só para a grade, sem aparecer. */
static u32  dono;                           /* 0 = ninguém */
static u32 *quadro_dono;

/* As 16 cores do modo texto, em RGB (um pouco mais suaves que as da VGA) */
static const u32 PALETA[16] = {
    0x0B1220, 0x2457C5, 0x2E9E4F, 0x2EC4D6, 0xC23B3B, 0x9B4DCA, 0xC77D1E, 0xC8CDD6,
    0x5C6675, 0x5B8DEF, 0x5FD07F, 0x7FE6F2, 0xFF6B6B, 0xD38CFF, 0xFFC94A, 0xFFFFFF
};

static void ponto(u32 x, u32 y, u32 c) { if (x < LARG && y < ALT) tela[y * LARG + x] = c; }

static void retangulo(u32 x, u32 y, u32 l, u32 a, u32 c)
{
    for (u32 j = 0; j < a; j++)
        for (u32 i = 0; i < l; i++) ponto(x + i, y + j, c);
}

/* Um desenho da fonte, ampliado 'escala' vezes, com o fundo 'fundo' (ou
 * transparente se fundo = 0xFFFFFFFF). */
static void letra(u32 x, u32 y, u32 glifo, u32 frente, u32 fundo, u32 escala)
{
    for (u32 j = 0; j < FONTE_ALTURA; j++) {
        u8 linha = fonte_glifos[glifo % FONTE_GLIFOS][j];
        for (u32 i = 0; i < FONTE_LARGURA; i++) {
            int liga = (linha >> (7 - i)) & 1;
            if (!liga && fundo == 0xFFFFFFFFu) continue;
            retangulo(x + i * escala, y + j * escala, escala, escala, liga ? frente : fundo);
        }
    }
}

/* Unicode -> desenho (busca binária na tabela da fonte); '?' se não tiver. */
static u32 glifo_de(u32 cp)
{
    u32 ini = 0, fim = FONTE_MAPA;
    while (ini < fim) {
        u32 meio = (ini + fim) / 2;
        if (fonte_mapa[meio][0] == cp) return fonte_mapa[meio][1];
        if (fonte_mapa[meio][0] < cp) ini = meio + 1; else fim = meio;
    }
    return cp == '?' ? 0 : glifo_de('?');
}

/* ---- A logo: o sol nascendo (arte/logo.svg), desenhada aqui mesmo ----
 * Cada ponto do ícone é a média de 4 × 4 amostras (bordas suaves). As
 * medidas são as do SVG, num quadrado de 256. */
static u32 mistura(u32 a, u32 b, u32 t /* 0-256 */)
{
    u32 r = (((a >> 16) & 255) * (256 - t) + ((b >> 16) & 255) * t) >> 8;
    u32 g = (((a >> 8) & 255) * (256 - t) + ((b >> 8) & 255) * t) >> 8;
    u32 bb = ((a & 255) * (256 - t) + (b & 255) * t) >> 8;
    return r << 16 | g << 8 | bb;
}

static int dentro_arredondado(int x, int y, int x0, int y0, int x1, int y1, int r)
{
    if (x < x0 || x >= x1 || y < y0 || y >= y1) return 0;
    int cx = x < x0 + r ? x0 + r : x >= x1 - r ? x1 - r : x;
    int cy = y < y0 + r ? y0 + r : y >= y1 - r ? y1 - r : y;
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}

static int perto_segmento(int x, int y, int ax, int ay, int bx, int by, int meia_largura)
{
    long dx = bx - ax, dy = by - ay, px = x - ax, py = y - ay;
    long l2 = dx * dx + dy * dy, t = l2 ? (px * dx + py * dy) * 1024 / l2 : 0;
    if (t < 0) t = 0;
    if (t > 1024) t = 1024;
    long qx = ax + dx * t / 1024 - x, qy = ay + dy * t / 1024 - y;
    return qx * qx + qy * qy <= (long)meia_largura * meia_largura;
}

/* A cor de uma amostra do ícone (coordenadas × 4: um quadrado de 1024),
 * ou 0xFFFFFFFF fora dele. */
static u32 logo_amostra(int x, int y)
{
    if (!dentro_arredondado(x, y, 0, 0, 1024, 1024, 224)) return 0xFFFFFFFFu;
    u32 c = mistura(0x0B1E3F, 0x1B3A6B, (u32)(y / 4));                 /* o céu */
    int dx = x - 512, dy = y - 672;
    if (y < 672 && dx * dx + dy * dy <= 248 * 248)                    /* o sol */
        c = mistura(0xFFC94A, 0xFF6A00, (u32)((y - 424) * 256 / 248));
    if (perto_segmento(x, y, 512, 240, 512, 336, 20) ||                /* os raios */
        perto_segmento(x, y, 240, 392, 304, 448, 20) ||
        perto_segmento(x, y, 784, 392, 720, 448, 20)) c = 0xFFC94A;
    if (dentro_arredondado(x, y, 160, 664, 864, 704, 20)) c = 0x2EC4D6;  /* o horizonte */
    if (dentro_arredondado(x, y, 288, 760, 736, 792, 16)) c = mistura(c, 0x2EC4D6, 140);
    if (dentro_arredondado(x, y, 400, 840, 624, 864, 12)) c = mistura(c, 0x2EC4D6, 77);
    int sx = x - 824, sy = y - 232;                                    /* a estrela */
    if (sx < 0) sx = -sx;
    if (sy < 0) sy = -sy;
    if (sx * 3 + sy * 1 <= 104 && sx * 1 + sy * 3 <= 104 && sx + sy <= 70) c = 0xFFFFFF;
    return c;
}

static void logo(u32 x0, u32 y0, u32 lado, u32 fundo)
{
    for (u32 y = 0; y < lado; y++)
        for (u32 x = 0; x < lado; x++) {
            u32 r = 0, g = 0, b = 0;
            for (u32 j = 0; j < 4; j++)
                for (u32 i = 0; i < 4; i++) {
                    u32 c = logo_amostra((int)(((x * 4 + i) * 1024 + 512) / (lado * 4)),
                                         (int)(((y * 4 + j) * 1024 + 512) / (lado * 4)));
                    if (c == 0xFFFFFFFFu) c = fundo;
                    r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255;
                }
            ponto(x0 + x, y0 + y, (r / 16) << 16 | (g / 16) << 8 | (b / 16));
        }
}

static void texto_grande(u32 x, u32 y, const char *t, u32 cor, u32 escala)
{
    for (; *t; t++, x += FONTE_LARGURA * escala) letra(x, y, glifo_de((u8)*t), cor, 0xFFFFFFFFu, escala);
}

/* A faixa de cima: a logo, o nome e a versão do núcleo. */
static void topo(void)
{
    const u32 fundo = 0x0B1E3F;
    retangulo(0, 0, LARG, TOPO, fundo);
    retangulo(0, TOPO - 2, LARG, 2, 0x2EC4D6);
    logo(16, 8, 64, fundo);
    texto_grande(96, 16, "Nova", 0xFFFFFF, 3);
    texto_grande(96 + 4 * 24, 16, "Era", 0xFF7A1A, 3);
    msg_t m;
    sys_nucleo(&m);
    u32 n = str_tam(m.texto);
    for (u32 i = 0; i < n; i++)
        letra(LARG - 16 - (n - i) * FONTE_LARGURA, 32, glifo_de((u8)m.texto[i]), 0x9FB3CF, 0xFFFFFFFFu, 1);
}

/* O fundo azul do tema do modo texto (COR() põe azul atrás de tudo) vira,
 * na tela gráfica, o fundo escuro do terminal. */
static u32 cor_fundo(u8 cor)
{
    u32 f = (cor >> 4) & 15;
    return f == 1 ? PALETA[0] : PALETA[f];
}

static void g_celula(u32 l, u32 c, u32 glifo, u8 cor)
{
    if (l < G_LINHAS && c < G_COLUNAS) { g_letras[l][c] = (u16)glifo; g_cores[l][c] = cor; }
    if (dono) return;
    letra(c * FONTE_LARGURA, TOPO + l * FONTE_ALTURA, glifo, PALETA[cor & 15], cor_fundo(cor), 1);
}

static void g_cursor(int mostrar)
{
    if (dono || g_lin >= G_LINHAS || g_col >= G_COLUNAS) return;
    u32 y = TOPO + g_lin * FONTE_ALTURA + FONTE_ALTURA - 2;
    retangulo(g_col * FONTE_LARGURA, y, FONTE_LARGURA, 2, mostrar ? 0xC8CDD6 : PALETA[0]);
}

static void g_rolar(void)
{
    for (u32 l = 0; l + 1 < G_LINHAS; l++)
        for (u32 c = 0; c < G_COLUNAS; c++) { g_letras[l][c] = g_letras[l + 1][c]; g_cores[l][c] = g_cores[l + 1][c]; }
    for (u32 c = 0; c < G_COLUNAS; c++) { g_letras[G_LINHAS - 1][c] = (u16)glifo_de(' '); g_cores[G_LINHAS - 1][c] = 0; }
    if (dono) return;
    u64 *d = (u64 *)(tela + TOPO * LARG);
    const u64 *o = (const u64 *)(tela + (TOPO + FONTE_ALTURA) * LARG);
    u64 n = (u64)(G_LINHAS - 1) * FONTE_ALTURA * LARG / 2;
    for (u64 i = 0; i < n; i++) d[i] = o[i];
    retangulo(0, TOPO + (G_LINHAS - 1) * FONTE_ALTURA, LARG, FONTE_ALTURA, PALETA[0]);
}

static void g_limpar(void)
{
    for (u32 l = 0; l < G_LINHAS; l++)
        for (u32 c = 0; c < G_COLUNAS; c++) { g_letras[l][c] = (u16)glifo_de(' '); g_cores[l][c] = 0; }
    if (!dono) retangulo(0, TOPO, LARG, ALT - TOPO, PALETA[0]);
    g_lin = g_col = 0;
}

/* Depois de uma tela cheia: o topo e o terminal, de novo. */
static void g_redesenhar(void)
{
    topo();
    for (u32 l = 0; l < G_LINHAS; l++)
        for (u32 c = 0; c < G_COLUNAS; c++)
            letra(c * FONTE_LARGURA, TOPO + l * FONTE_ALTURA, g_letras[l][c],
                  PALETA[g_cores[l][c] & 15], cor_fundo(g_cores[l][c]), 1);
    g_cursor(1);
}

static void g_putc(u32 glifo, int especial, u8 cor)
{
    g_cursor(0);
    if (especial == '\n') {
        g_col = 0;
        g_lin++;
    } else if (especial == '\b') {
        if (g_col > 0)        g_col--;
        else if (g_lin > 0)   { g_lin--; g_col = G_COLUNAS - 1; }
        g_celula(g_lin, g_col, glifo_de(' '), cor);
    } else {
        g_celula(g_lin, g_col, glifo, cor);
        if (++g_col >= G_COLUNAS) { g_col = 0; g_lin++; }
    }
    if (g_lin >= G_LINHAS) { g_rolar(); g_lin = G_LINHAS - 1; }
    g_cursor(1);
}

static void seta_mostrar(void);

/* ---- A placa BGA: portas 0x1CE (índice) e 0x1CF (valor); a memória da
 * tela é a 1ª região (BAR0) do dispositivo no barramento PCI. ---- */
#define BGA_INDICE 0x1CE
#define BGA_DADOS  0x1CF

static u16 bga_le(u16 i)          { outw(BGA_INDICE, i); return inw(BGA_DADOS); }
static void bga_poe(u16 i, u16 v) { outw(BGA_INDICE, i); outw(BGA_DADOS, v); }

static u32 pci_le(u32 bus, u32 disp, u32 func, u32 reg)
{
    outl(0xCF8, 0x80000000u | bus << 16 | disp << 11 | func << 8 | (reg & 0xFC));
    return inl(0xCFC);
}

/* Acha a placa (classe 03 = vídeo) e devolve o endereço físico da tela. */
static u64 bga_memoria(void)
{
    for (u32 bus = 0; bus < 8; bus++)
        for (u32 d = 0; d < 32; d++) {
            u32 id = pci_le(bus, d, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF) continue;
            u32 classe = pci_le(bus, d, 0, 8) >> 24;
            u32 fab = id & 0xFFFF;
            if (classe == 0x03 && (fab == 0x1234 || fab == 0x80EE))   /* QEMU/Bochs, VirtualBox */
                return pci_le(bus, d, 0, 0x10) & ~0xFull;
        }
    return 0;
}

/* A tabela CP437 da placa VGA (os bytes 128-255) em Unicode: o que o
 * núcleo escreveu no boot (molduras, acentos) aparece igual na tela nova. */
static const u16 CP437[128] = { 0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5, 0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192, 0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB, 0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, 0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580, 0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229, 0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0 };

/* Liga o modo gráfico; 0 se não deu (e a tela continua em texto). */
static int grafico_iniciar(void)
{
    u16 id = bga_le(0);
    if (id < 0xB0C0 || id > 0xB0C5) return 0;            /* não há BGA */
    u64 fis = bga_memoria();
    if (!fis) return 0;
    long v = sys_mapear_fisico(fis, (u64)LARG * ALT * 4);
    if (v < 0) return 0;
    /* guarda o que o núcleo escreveu no boot (a tela de texto) */
    static u16 boot[COLUNAS * LINHAS];
    for (u32 i = 0; i < COLUNAS * LINHAS; i++) boot[i] = mem_le16(USUARIO_VGA + 2 * i);
    u32 ate = lin;
    bga_poe(4, 0);                                        /* desliga para trocar */
    bga_poe(1, LARG);
    bga_poe(2, ALT);
    bga_poe(3, 32);
    bga_poe(4, 0x41);                                     /* liga, com a memória linear */
    tela = (u32 *)v;
    grafico = 1;
    topo();
    g_limpar();
    for (u32 l = 0; l < ate && l < LINHAS; l++) {         /* as mensagens do boot, de novo */
        for (u32 c = 0; c < COLUNAS; c++) {
            u16 cel = boot[l * COLUNAS + c];
            u32 ch = (u8)cel;
            if (ch >= 128) ch = CP437[ch - 128];          /* a placa VGA usa a tabela CP437 */
            g_celula(g_lin, c, ch >= 32 && ch != 127 ? glifo_de(ch) : glifo_de(' '), (u8)(cel >> 8));
        }
        g_lin++;
    }
    g_col = 0;
    g_cursor(1);
    seta_mostrar();
    return 1;
}

/* A tela: 80x25 células de 2 bytes (caractere + cor) em USUARIO_VGA. */
static void celula(u32 i, u16 valor) { mem_escreve16(USUARIO_VGA + 2 * i, valor); }

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

/* ------------------------- Porta serial (COM1) ------------------------- */

#define COM1 0x3F8u

static void serial_iniciar(void)
{
    outb(COM1 + 1, 0x00);     /* sem interrupções */
    outb(COM1 + 3, 0x80);     /* DLAB: vai definir a velocidade */
    outb(COM1 + 0, 0x01);     /* divisor 1 = 115200 bps */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);     /* 8 bits, sem paridade, 1 bit de parada */
    outb(COM1 + 2, 0xC7);     /* liga e limpa as filas (FIFO) */
}

static void serial_byte(u8 b)
{
    while (!(inb(COM1 + 5) & 0x20)) { }   /* espera a saída esvaziar */
    outb(COM1, b);
}

static void serial_escreve(u8 b)
{
    if (b == '\n')      serial_byte('\r');
    else if (b == '\b') { serial_byte('\b'); serial_byte(' '); }
    serial_byte(b);
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

/* -------------------------------- A seta ----------------------------------
 * O desenho da seta (12 × 19): '#' = contorno preto, '.' = branco. Antes
 * de desenhar, guarda o que havia embaixo; para mover ou para escrever
 * texto ali, devolve o que estava (esconder) e desenha de novo (mostrar). */
#define SETA_L 12u
#define SETA_A 19u
static const char *const SETA[SETA_A] = {
    "#           ", "##          ", "#.#         ", "#..#        ", "#...#       ",
    "#....#      ", "#.....#     ", "#......#    ", "#.......#   ", "#........#  ",
    "#.........# ", "#..........#", "#......#####", "#...#..#    ", "#..# #..#   ",
    "#.#  #..#   ", "##    #..#  ", "      #..#  ", "       ##   "
};
static u32 seta_embaixo[SETA_L * SETA_A];
static int seta_x = LARG / 2, seta_y = ALT / 2, seta_visivel;
static u32 seta_botoes;
static long seta_roda;

static void seta_esconder(void)
{
    if (!seta_visivel) return;
    for (u32 j = 0; j < SETA_A; j++)
        for (u32 i = 0; i < SETA_L; i++)
            if (SETA[j][i] != ' ') ponto((u32)seta_x + i, (u32)seta_y + j, seta_embaixo[j * SETA_L + i]);
    seta_visivel = 0;
}

static void seta_mostrar(void)
{
    if (seta_visivel || !grafico) return;
    for (u32 j = 0; j < SETA_A; j++)
        for (u32 i = 0; i < SETA_L; i++) {
            u32 x = (u32)seta_x + i, y = (u32)seta_y + j;
            if (SETA[j][i] == ' ' || x >= LARG || y >= ALT) continue;
            seta_embaixo[j * SETA_L + i] = tela[y * LARG + x];
            tela[y * LARG + x] = SETA[j][i] == '#' ? 0x000000 : 0xFFFFFF;
        }
    seta_visivel = 1;
}

/* ----------------- Quem quer saber do mouse (MOUSE_ESPERAR) ---------------
 * Cada programa que pergunta ganha uma fila de eventos. O servidor nunca
 * espera ninguém: quem pergunta e não há evento fica sem resposta até
 * chegar um (a resposta é adiada, como nos pipes). Movimentos seguidos com
 * os mesmos botões viram um só (o último). */
#define OBSERVADORES 4u
#define FILA_EV      16u
typedef struct { u32 x, y, botoes; long roda; } evento_t;
typedef struct {
    u32 pid;                     /* 0 = livre */
    int esperando;
    evento_t fila[FILA_EV];
    u32 ini, n;
} observador_t;
static observador_t obs[OBSERVADORES];

static int ob_responder(observador_t *o)
{
    evento_t *e = &o->fila[o->ini];
    msg_t r;
    msg_zera(&r);
    r.tipo   = MSG_OK;
    r.arg[0] = e->x;
    r.arg[1] = e->y;
    r.arg[2] = e->botoes;
    r.arg[3] = (u32)e->roda;
    o->ini = (o->ini + 1) % FILA_EV;
    o->n--;
    o->esperando = 0;
    if (sys_responder(o->pid, &r) < 0) { o->pid = 0; return 0; }   /* foi embora */
    return 1;
}

static void mouse_evento(u32 roda_mudou)
{
    for (u32 i = 0; i < OBSERVADORES; i++) {
        observador_t *o = &obs[i];
        if (!o->pid) continue;
        evento_t *ult = o->n ? &o->fila[(o->ini + o->n - 1) % FILA_EV] : 0;
        if (ult && ult->botoes == seta_botoes && !roda_mudou && !ult->roda && !o->esperando) {
            ult->x = (u32)seta_x;                    /* só andou: atualiza o último */
            ult->y = (u32)seta_y;
        } else {
            if (o->n == FILA_EV) { o->ini = (o->ini + 1) % FILA_EV; o->n--; }   /* cheia: perde o mais velho */
            evento_t *e = &o->fila[(o->ini + o->n) % FILA_EV];
            e->x = (u32)seta_x;
            e->y = (u32)seta_y;
            e->botoes = seta_botoes;
            e->roda = roda_mudou ? seta_roda : 0;
            o->n++;
        }
        if (o->esperando) ob_responder(o);
    }
    seta_roda = 0;
}

static void mouse_esperar(u32 quem)
{
    observador_t *o = 0;
    for (u32 i = 0; i < OBSERVADORES && !o; i++) if (obs[i].pid == quem) o = &obs[i];
    for (u32 i = 0; i < OBSERVADORES && !o; i++)
        if (!obs[i].pid) { o = &obs[i]; mem_zera(o, sizeof *o); o->pid = quem; }
    if (!o) {                                      /* lotado: o mais antigo sai */
        o = &obs[0];
        mem_zera(o, sizeof *o);
        o->pid = quem;
    }
    o->esperando = 1;
    if (o->n) ob_responder(o);
}

static void mouse_movimento(const msg_t *m)
{
    seta_esconder();
    if (m->arg[2] & MOUSE_ABSOLUTO) {              /* o vmmouse: a posição exata (0-65535) */
        /* o QEMU manda x × 65534 / largura (sem o -1): a conta de volta */
        seta_x = (int)(((u64)m->arg[0] * LARG + 32767) / 65534);
        seta_y = (int)(((u64)m->arg[1] * ALT + 32767) / 65534);
    } else {
        seta_x += (int)m->arg[0];
        seta_y += (int)m->arg[1];
    }
    if (seta_x < 0) seta_x = 0;
    if (seta_y < 0) seta_y = 0;
    if (seta_x > (int)LARG - 1) seta_x = (int)LARG - 1;
    if (seta_y > (int)ALT - 1) seta_y = (int)ALT - 1;
    seta_botoes = m->arg[2] & 7;
    seta_roda += (int)m->arg[3];
    seta_mostrar();
    mouse_evento(m->arg[3] != 0);
}

/* ------------------------------ Tela cheia -------------------------------- */

static void tela_responder(u32 para, long erro, u32 a0, u32 a1)
{
    msg_t r;
    msg_zera(&r);
    r.tipo   = erro ? MSG_ERRO : MSG_OK;
    r.arg[0] = erro ? (u32)(-erro) : a0;
    r.arg[1] = a1;
    sys_responder(para, &r);
}

static int vivo(u32 pid)
{
    msg_t m;
    if (sys_tarefa_info(pid, &m) < 0) return 0;
    return m.arg[0] != ESTADO_LIVRE && m.arg[0] != ESTADO_MORTO && m.arg[0] != ESTADO_ZUMBI;
}

static int console_velho;                   /* a tela ainda mostra a imagem do último dono */

static void tela_fechar_com(int manter)
{
    if (!dono) return;
    sys_munmap((u64)quadro_dono, (u64)LARG * ALT * 4);
    dono = 0;
    quadro_dono = 0;
    if (manter) { console_velho = 1; return; }   /* (outro programa já vai abrir: sem piscar o console) */
    console_velho = 0;
    seta_esconder();
    g_redesenhar();
    seta_mostrar();
}

static void tela_fechar(void) { tela_fechar_com(0); }

static void tela_pedido(const msg_t *m)
{
    u32 quem = m->origem;
    if (!grafico) { tela_responder(quem, ERRO_INVALIDO, 0, 0); return; }
    if (m->tipo == VIDEO_TELA_ABRIR) {
        if (dono && dono != quem && vivo(dono)) { tela_responder(quem, ERRO_OCUPADO, 0, 0); return; }
        tela_fechar();
        mmap_t p;
        p.tam = (u64)LARG * ALT * 4;
        p.objeto = m->arg[0];
        p.prot = PROT_LER;
        p.reservado = 0;
        long v = sys_mmap(&p);
        if (v < 0) { tela_responder(quem, v, 0, 0); return; }
        quadro_dono = (u32 *)v;
        dono = quem;
        console_velho = 0;
        tela_responder(quem, 0, LARG, ALT);
        return;
    }
    if (quem != dono) { tela_responder(quem, ERRO_SEM_PERMISSAO, 0, 0); return; }
    if (m->tipo == VIDEO_TELA_FECHAR) { tela_fechar_com(m->arg[0] == 1); tela_responder(quem, 0, 0, 0); return; }
    /* VIDEO_TELA_MOSTRAR: copia o retângulo pedido */
    u32 x = m->arg[0], y = m->arg[1], l = m->arg[2], a = m->arg[3];
    if (x >= LARG || y >= ALT) { tela_responder(quem, 0, 0, 0); return; }
    if (l > LARG - x) l = LARG - x;
    if (a > ALT - y) a = ALT - y;
    seta_esconder();
    for (u32 j = 0; j < a; j++) {
        u32 *d = tela + (y + j) * LARG + x;
        const u32 *o = quadro_dono + (y + j) * LARG + x;
        for (u32 i = 0; i < l; i++) d[i] = o[i];
    }
    seta_mostrar();
    tela_responder(quem, 0, 0, 0);
}

/* No modo gráfico, cada código Unicode vira um desenho da fonte. */
static void saida(u32 cp, u8 cor)
{
    if (!grafico) { putc((char)(cp < 128 ? cp : cp437_latin(cp)), cor); return; }
    if (console_velho && !dono) {                /* alguém escreveu: o console volta */
        console_velho = 0;
        seta_esconder();
        g_redesenhar();
        seta_mostrar();
    }
    if (cp == '\n' || cp == '\b') g_putc(0, (int)cp, cor);
    else g_putc(glifo_de(cp), 0, cor);
}

/* As sequências do terminal (as do Linux): Esc [ n D (o cursor n letras para
 * a esquerda), Esc [ n C (para a direita), Esc [ K (apaga até o fim da linha).
 * O terminal usa para editar a linha (as setas, o histórico). */
static void cursor_andar(int n)
{
    if (!grafico) {
        while (n < 0) { if (col > 0) col--; else if (lin > 0) { lin--; col = COLUNAS - 1; } n++; }
        while (n > 0) { if (++col >= COLUNAS) { col = 0; if (lin + 1 < LINHAS) lin++; } n--; }
        return;
    }
    g_cursor(0);
    while (n < 0) { if (g_col > 0) g_col--; else if (g_lin > 0) { g_lin--; g_col = G_COLUNAS - 1; } n++; }
    while (n > 0) { if (++g_col >= G_COLUNAS) { g_col = 0; if (g_lin + 1 < G_LINHAS) g_lin++; } n--; }
    g_cursor(1);
}

static void apagar_fim_da_linha(u8 cor)
{
    if (!grafico) {
        for (u32 c = col; c < COLUNAS; c++) celula(lin * COLUNAS + c, (u16)(cor << 8 | ' '));
        return;
    }
    g_cursor(0);
    for (u32 c = g_col; c < G_COLUNAS; c++) g_celula(g_lin, c, glifo_de(' '), cor);
    g_cursor(1);
}

static u32 esc_estado, esc_num;          /* 0: texto; 1: veio o Esc; 2: dentro de Esc [ */

static u8  utf8_lider;        /* 1º byte de uma sequência em andamento */
static u32 utf8_resta;        /* quantos bytes de continuação faltam */
static u32 utf8_valor;

static void escreve_byte(u8 b, u8 cor)
{
    if (esc_estado == 1) { esc_estado = b == '[' ? 2 : 0; esc_num = 0; return; }
    if (esc_estado == 2) {
        if (b >= '0' && b <= '9') { esc_num = esc_num * 10 + (b - '0'); return; }
        esc_estado = 0;
        int n = esc_num ? (int)esc_num : 1;
        if (b == 'D') cursor_andar(-n);
        else if (b == 'C') cursor_andar(n);
        else if (b == 'K') apagar_fim_da_linha(cor);
        return;
    }
    if (b == 27 && !utf8_resta) { esc_estado = 1; return; }
    if (utf8_resta) {
        if ((b & 0xC0) == 0x80) {                    /* continuação */
            utf8_valor = (utf8_valor << 6) | (b & 0x3F);
            if (--utf8_resta == 0) saida(utf8_valor, cor);
            return;
        }
        if (grafico) saida('?', cor);                /* não era UTF-8 */
        else putc((char)utf8_lider, cor);            /* era CP437 */
        utf8_resta = 0;
    }
    if (b >= 0xC2 && b <= 0xDF)      { utf8_lider = b; utf8_resta = 1; utf8_valor = b & 0x1F; }
    else if (b >= 0xE0 && b <= 0xEF) { utf8_lider = b; utf8_resta = 2; utf8_valor = b & 0x0F; }
    else if (b >= 0xF0 && b <= 0xF4) { utf8_lider = b; utf8_resta = 3; utf8_valor = b & 0x07; }
    else if (grafico) saida(b < 128 ? b : '?', cor);
    else putc((char)b, cor);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    cursor_ler();
    serial_iniciar();

    /* "grafico" na linha de boot: a tela gráfica, se houver a placa */
    msg_t b;
    sys_linha_boot(&b);
    for (u32 i = 0; b.texto[i]; i++)
        if (mem_igual(b.texto + i, "grafico", 7)) { grafico_iniciar(); break; }

    msg_t m;
    for (;;) {
        sys_receber(PID_QUALQUER, &m);

        if (m.tipo == MSG_MOUSE && m.origem == PID_TECLADO) {
            if (grafico) mouse_movimento(&m);
            continue;
        }
        if (m.tipo == MOUSE_ESPERAR) {
            if (!grafico) {                         /* o mouse só existe na tela gráfica */
                msg_t r;
                msg_zera(&r);
                r.tipo = MSG_ERRO;
                r.arg[0] = (u32)(-ERRO_INVALIDO);
                sys_responder(m.origem, &r);
            } else mouse_esperar(m.origem);
            continue;
        }
        if (m.tipo == VIDEO_TELA_ABRIR || m.tipo == VIDEO_TELA_MOSTRAR || m.tipo == VIDEO_TELA_FECHAR) {
            tela_pedido(&m);
            continue;
        }
        if (dono && !vivo(dono)) tela_fechar();     /* o dono da tela cheia morreu sem fechar */
        seta_esconder();                            /* o texto pode passar embaixo da seta */
        if (m.tipo == MSG_VIDEO_ESCREVER) {
            for (u32 i = 0; i < MSG_TEXTO_MAX && m.texto[i]; i++) {
                escreve_byte((u8)m.texto[i], (u8)m.arg[0]);
                serial_escreve((u8)m.texto[i]);
            }
        } else if (m.tipo == MSG_VIDEO_LIMPAR) {
            if (grafico) { g_cursor(0); g_limpar(); g_cursor(1); }
            else limpar();
        }
        if (!grafico) cursor_mover();
        seta_mostrar();
    }
}
