/*
 * nlibc - libc/src/grafico.c
 * As funções de desenho de grafico.h. Só desenham na memória: este arquivo
 * compila igual no Linux (é assim que o teste confere os desenhos). A tela
 * e o mouse estão em grafico-tela.c.
 */
#include <grafico.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "fonte8x16.h"

gr_imagem_t *gr_nova(int l, int a)
{
    if (l <= 0 || a <= 0) return NULL;
    gr_imagem_t *im = calloc(1, sizeof *im);
    if (!im) return NULL;
    im->pontos = calloc((size_t)l * (size_t)a, sizeof(gr_cor));
    if (!im->pontos) { free(im); return NULL; }
    im->largura = l;
    im->altura = a;
    im->passo = l;
    return im;
}

void gr_liberar(gr_imagem_t *im)
{
    if (!im) return;
    if (!im->_tela) free(im->pontos);
    free(im);
}

/* a sobre b (A de a: 0-255) */
static gr_cor misturar(gr_cor a, gr_cor b)
{
    unsigned al = a >> 24;
    if (al == 255) return a;
    if (al == 0) return b;
    unsigned bl = b >> 24, inv = 255 - al;
    unsigned r = (((a >> 16) & 255) * al + ((b >> 16) & 255) * inv + 127) / 255;
    unsigned g = (((a >> 8) & 255) * al + ((b >> 8) & 255) * inv + 127) / 255;
    unsigned bb = ((a & 255) * al + (b & 255) * inv + 127) / 255;
    unsigned o = al + (bl * inv + 127) / 255;
    return o << 24 | r << 16 | g << 8 | bb;
}

void gr_ponto(gr_imagem_t *im, int x, int y, gr_cor c)
{
    if (x < 0 || y < 0 || x >= im->largura || y >= im->altura) return;
    gr_cor *p = &im->pontos[(size_t)y * (size_t)im->passo + (size_t)x];
    *p = misturar(c, *p);
}

gr_cor gr_ler(const gr_imagem_t *im, int x, int y)
{
    if (x < 0 || y < 0 || x >= im->largura || y >= im->altura) return 0;
    return im->pontos[(size_t)y * (size_t)im->passo + (size_t)x];
}

void gr_linha(gr_imagem_t *im, int x0, int y0, int x1, int y1, gr_cor c)
{
    /* Bresenham: anda um ponto de cada vez, escolhendo o mais perto da reta */
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int erro = dx + dy;
    for (;;) {
        gr_ponto(im, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * erro;
        if (e2 >= dy) { erro += dy; x0 += sx; }
        if (e2 <= dx) { erro += dx; y0 += sy; }
    }
}

void gr_preencher(gr_imagem_t *im, int x, int y, int l, int a, gr_cor c)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + l > im->largura ? im->largura : x + l, y1 = y + a > im->altura ? im->altura : y + a;
    for (int j = y0; j < y1; j++) {
        gr_cor *p = &im->pontos[(size_t)j * (size_t)im->passo];
        if ((c >> 24) == 255) for (int i = x0; i < x1; i++) p[i] = c;
        else for (int i = x0; i < x1; i++) p[i] = misturar(c, p[i]);
    }
}

void gr_retangulo(gr_imagem_t *im, int x, int y, int l, int a, gr_cor c)
{
    if (l <= 0 || a <= 0) return;
    gr_preencher(im, x, y, l, 1, c);
    if (a > 1) gr_preencher(im, x, y + a - 1, l, 1, c);
    if (a > 2) {
        gr_preencher(im, x, y + 1, 1, a - 2, c);
        if (l > 1) gr_preencher(im, x + l - 1, y + 1, 1, a - 2, c);
    }
}

void gr_circulo(gr_imagem_t *im, int cx, int cy, int r, gr_cor c)
{
    /* o algoritmo do ponto médio: um oitavo do círculo, espelhado 8 vezes */
    int x = r, y = 0, erro = 1 - r;
    while (x >= y) {
        gr_ponto(im, cx + x, cy + y, c); gr_ponto(im, cx - x, cy + y, c);
        if (y) { gr_ponto(im, cx + x, cy - y, c); gr_ponto(im, cx - x, cy - y, c); }
        if (x != y) {
            gr_ponto(im, cx + y, cy + x, c); gr_ponto(im, cx + y, cy - x, c);
            if (y) { gr_ponto(im, cx - y, cy + x, c); gr_ponto(im, cx - y, cy - x, c); }
        }
        y++;
        if (erro < 0) erro += 2 * y + 1;
        else { x--; erro += 2 * (y - x) + 1; }
    }
}

void gr_disco(gr_imagem_t *im, int cx, int cy, int r, gr_cor c)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
        gr_preencher(im, cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}

static gr_cor entre(gr_cor a, gr_cor b, int t, int total)   /* t de 0 a total */
{
    if (total <= 0) return a;
    gr_cor r = 0;
    for (int k = 0; k < 32; k += 8) {
        int ca = (int)((a >> k) & 255), cb = (int)((b >> k) & 255);
        r |= (gr_cor)((ca + (cb - ca) * t / total) & 255) << k;
    }
    return r;
}

void gr_degrade(gr_imagem_t *im, int x, int y, int l, int a, gr_cor de, gr_cor ate, int vertical)
{
    if (vertical) for (int j = 0; j < a; j++) gr_preencher(im, x, y + j, l, 1, entre(de, ate, j, a - 1));
    else for (int i = 0; i < l; i++) gr_preencher(im, x + i, y, 1, a, entre(de, ate, i, l - 1));
}

void gr_copiar(gr_imagem_t *dest, int x, int y, const gr_imagem_t *orig)
{
    for (int j = 0; j < orig->altura; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= dest->altura) continue;
        for (int i = 0; i < orig->largura; i++) {
            int dx = x + i;
            if (dx < 0 || dx >= dest->largura) continue;
            gr_cor *p = &dest->pontos[(size_t)dy * (size_t)dest->passo + (size_t)dx];
            *p = misturar(orig->pontos[(size_t)j * (size_t)orig->passo + (size_t)i], *p);
        }
    }
}

/* Aumenta ou diminui a imagem para l × a, com a média dos 4 pontos mais
 * perto de cada um (bilinear): fica suave, sem "escadinhas". */
void gr_escalar(gr_imagem_t *dest, int x, int y, int l, int a, const gr_imagem_t *orig)
{
    if (l <= 0 || a <= 0) return;
    for (int j = 0; j < a; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= dest->altura) continue;
        long sy = ((long)j * 2 + 1) * orig->altura * 128 / a - 128;       /* em 1/256 de ponto */
        if (sy < 0) sy = 0;
        int y0 = (int)(sy >> 8), fy = (int)(sy & 255);
        int y1 = y0 + 1 < orig->altura ? y0 + 1 : y0;
        for (int i = 0; i < l; i++) {
            int dx = x + i;
            if (dx < 0 || dx >= dest->largura) continue;
            long sx = ((long)i * 2 + 1) * orig->largura * 128 / l - 128;
            if (sx < 0) sx = 0;
            int x0 = (int)(sx >> 8), fx = (int)(sx & 255);
            int x1 = x0 + 1 < orig->largura ? x0 + 1 : x0;
            gr_cor c00 = gr_ler(orig, x0, y0), c10 = gr_ler(orig, x1, y0);
            gr_cor c01 = gr_ler(orig, x0, y1), c11 = gr_ler(orig, x1, y1);
            gr_cor c = 0;
            for (int k = 0; k < 32; k += 8) {
                int v0 = (int)((c00 >> k) & 255) * (256 - fx) + (int)((c10 >> k) & 255) * fx;
                int v1 = (int)((c01 >> k) & 255) * (256 - fx) + (int)((c11 >> k) & 255) * fx;
                c |= (gr_cor)(((v0 * (256 - fy) + v1 * fy + 32768) >> 16) & 255) << k;
            }
            gr_cor *p = &dest->pontos[(size_t)dy * (size_t)dest->passo + (size_t)dx];
            *p = misturar(c, *p);
        }
    }
}

static unsigned glifo(unsigned cp)
{
    unsigned ini = 0, fim = FONTE_MAPA;
    while (ini < fim) {
        unsigned meio = (ini + fim) / 2;
        if (fonte_mapa[meio][0] == cp) return fonte_mapa[meio][1];
        if (fonte_mapa[meio][0] < cp) ini = meio + 1; else fim = meio;
    }
    return cp == '?' ? 0 : glifo('?');
}

int gr_texto(gr_imagem_t *im, int x, int y, const char *s, gr_cor c, int escala)
{
    if (escala < 1) escala = 1;
    int x0 = x;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned cp = *p++;
        if (cp >= 0xC0) {                                /* UTF-8: 2 ou 3 bytes */
            int mais = cp >= 0xE0 ? 2 : 1;
            cp &= mais == 2 ? 0x0F : 0x1F;
            while (mais-- && (*p & 0xC0) == 0x80) cp = cp << 6 | (*p++ & 0x3F);
        }
        if (cp == '\n') { x = x0; y += (int)FONTE_ALTURA * escala; continue; }
        const unsigned char *g = fonte_glifos[glifo(cp) % FONTE_GLIFOS];
        for (int j = 0; j < (int)FONTE_ALTURA; j++)
            for (int i = 0; i < (int)FONTE_LARGURA; i++)
                if ((g[j] >> (7 - i)) & 1) gr_preencher(im, x + i * escala, y + j * escala, escala, escala, c);
        x += (int)FONTE_LARGURA * escala;
    }
    return x - x0;
}

int gr_salvar_ppm(const gr_imagem_t *im, const char *caminho)
{
    FILE *f = fopen(caminho, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", im->largura, im->altura);
    for (int j = 0; j < im->altura; j++)
        for (int i = 0; i < im->largura; i++) {
            gr_cor c = gr_ler(im, i, j);
            fputc((int)((c >> 16) & 255), f);
            fputc((int)((c >> 8) & 255), f);
            fputc((int)(c & 255), f);
        }
    return fclose(f) == 0 ? 0 : -1;
}

