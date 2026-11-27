/*
 * nlibc - libc/src/fonte.c
 * As letras bonitas de grafico.h: abre fontes TrueType com a stb_truetype
 * (de Sean Barrett, domínio público: libc/src/stb/stb_truetype.h) e
 * desenha cada letra com o contorno suave (anti-aliasing).
 *
 * Uma letra de uma fonte TrueType é um desenho de curvas, não de pontos. A
 * stb_truetype "pinta" as curvas numa grade do tamanho pedido e diz, para
 * cada ponto, quanto dele ficou dentro da letra (0 a 255): é essa cobertura
 * que vira a transparência, e a borda fica suave. Como pintar custa, cada
 * letra pintada fica guardada (o cache) para a próxima vez.
 *
 * Como em grafico.c, nada aqui depende do NovaEra: compila igual no Linux.
 */
#include <grafico.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

#define SUBPONTOS 4                 /* a letra pode começar em 1/4 de ponto */
#define CACHE     512               /* letras pintadas guardadas */

typedef struct {
    int cp, tamanho, sub;           /* cp = -1: vazio */
    int glifo, avanco;              /* avanco: nas unidades da fonte */
    int x0, y0, l, a;               /* a caixa da letra, em relação à linha de base */
    unsigned char *cobertura;
} letra_t;

struct gr_fonte {
    stbtt_fontinfo info;
    unsigned char *dados;
    int subida, descida, entre;     /* ascent, descent, lineGap (unidades da fonte) */
    letra_t cache[CACHE];
};

static unsigned char *ler_arquivo(const char *caminho)
{
    FILE *f = fopen(caminho, "rb");
    if (!f) return NULL;
    size_t cap = 65536, tam = 0, n;
    unsigned char *b = malloc(cap);
    while (b && (n = fread(b + tam, 1, cap - tam, f)) > 0) {
        tam += n;
        if (tam == cap) {
            unsigned char *maior = realloc(b, cap * 2);
            if (!maior) { free(b); b = NULL; break; }
            b = maior;
            cap *= 2;
        }
    }
    fclose(f);
    return b;
}

gr_fonte_t *gr_fonte_abrir(const char *caminho)
{
    gr_fonte_t *f = calloc(1, sizeof *f);
    if (!f) return NULL;
    f->dados = ler_arquivo(caminho);
    if (!f->dados || !stbtt_InitFont(&f->info, f->dados, stbtt_GetFontOffsetForIndex(f->dados, 0))) {
        free(f->dados);
        free(f);
        return NULL;
    }
    stbtt_GetFontVMetrics(&f->info, &f->subida, &f->descida, &f->entre);
    for (int i = 0; i < CACHE; i++) f->cache[i].cp = -1;
    return f;
}

void gr_fonte_fechar(gr_fonte_t *f)
{
    if (!f) return;
    for (int i = 0; i < CACHE; i++) free(f->cache[i].cobertura);
    free(f->dados);
    free(f);
}

static float escala(gr_fonte_t *f, int tamanho) { return stbtt_ScaleForPixelHeight(&f->info, (float)tamanho); }

/* A letra cp no tamanho dado, começando sub/SUBPONTOS de ponto à direita */
static letra_t *letra(gr_fonte_t *f, int cp, int tamanho, int sub)
{
    unsigned h = ((unsigned)cp * 2654435761u ^ (unsigned)tamanho * 40503u ^ (unsigned)sub) % CACHE;
    letra_t *l = &f->cache[h];
    if (l->cp == cp && l->tamanho == tamanho && l->sub == sub) return l;
    free(l->cobertura);
    memset(l, 0, sizeof *l);
    float e = escala(f, tamanho), dx = (float)sub / SUBPONTOS;
    l->cp = cp;
    l->tamanho = tamanho;
    l->sub = sub;
    l->glifo = stbtt_FindGlyphIndex(&f->info, cp);
    int lsb;
    stbtt_GetGlyphHMetrics(&f->info, l->glifo, &l->avanco, &lsb);
    int x1, y1;
    stbtt_GetGlyphBitmapBoxSubpixel(&f->info, l->glifo, e, e, dx, 0, &l->x0, &l->y0, &x1, &y1);
    l->l = x1 - l->x0;
    l->a = y1 - l->y0;
    if (l->l > 0 && l->a > 0) {
        l->cobertura = malloc((size_t)l->l * (size_t)l->a);
        if (l->cobertura)
            stbtt_MakeGlyphBitmapSubpixel(&f->info, l->cobertura, l->l, l->a, l->l, e, e, dx, 0, l->glifo);
        else l->l = l->a = 0;
    }
    return l;
}

/* o próximo caractere UTF-8 (um byte inválido vira ele mesmo) */
static int utf8(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    int c = *p++, n = 0;
    if (c >= 0xF0 && c < 0xF8) { c &= 7; n = 3; }
    else if (c >= 0xE0) { c &= 15; n = 2; }
    else if (c >= 0xC0) { c &= 31; n = 1; }
    for (int i = 0; i < n; i++) {
        if ((*p & 0xC0) != 0x80) { c = (unsigned char)**s; p = (const unsigned char *)*s + 1; break; }
        c = c << 6 | (*p++ & 63);
    }
    *s = (const char *)p;
    return c;
}

/* Desenha (im != NULL) ou só mede; devolve a largura da linha mais larga */
static int escrever(gr_imagem_t *im, gr_fonte_t *f, int x, int y, const char *s, int tamanho, gr_cor c)
{
    if (!f || !s || tamanho <= 0) return 0;
    float e = escala(f, tamanho);
    int base = y + (int)(f->subida * e + 0.5f), linha = gr_fonte_altura(f, tamanho);
    unsigned alfa = c >> 24;
    float caneta = 0, maior = 0;
    int antes = 0;                                   /* o glifo anterior, para o kerning */
    while (*s) {
        int cp = utf8(&s);
        if (cp == '\n') {
            if (caneta > maior) maior = caneta;
            caneta = 0;
            antes = 0;
            base += linha;
            continue;
        }
        float px = x + caneta;
        int inteiro = (int)floor(px), sub = (int)((px - inteiro) * SUBPONTOS);
        letra_t *l = letra(f, cp, tamanho, sub);
        if (antes) {                                 /* "AV": o V chega mais perto do A */
            float k = stbtt_GetGlyphKernAdvance(&f->info, antes, l->glifo) * e;
            if (k != 0) {
                caneta += k;
                px = x + caneta;
                inteiro = (int)floor(px);
                sub = (int)((px - inteiro) * SUBPONTOS);
                l = letra(f, cp, tamanho, sub);
            }
        }
        if (im && l->cobertura)
            for (int j = 0; j < l->a; j++)
                for (int i = 0; i < l->l; i++) {
                    unsigned cob = l->cobertura[j * l->l + i];
                    if (cob) gr_ponto(im, inteiro + l->x0 + i, base + l->y0 + j,
                                      (c & 0xFFFFFF) | ((alfa * cob + 127) / 255) << 24);
                }
        caneta += l->avanco * e;
        antes = l->glifo;
    }
    if (caneta > maior) maior = caneta;
    return (int)ceil(maior);
}

int gr_fonte_texto(gr_imagem_t *im, gr_fonte_t *f, int x, int y, const char *s, int tamanho, gr_cor c)
{
    return im ? escrever(im, f, x, y, s, tamanho, c) : 0;
}

int gr_fonte_largura(gr_fonte_t *f, const char *s, int tamanho) { return escrever(NULL, f, 0, 0, s, tamanho, 0); }

int gr_fonte_altura(gr_fonte_t *f, int tamanho)
{
    if (!f) return 0;
    return (int)((f->subida - f->descida + f->entre) * escala(f, tamanho) + 0.5f);
}
