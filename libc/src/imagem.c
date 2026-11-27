/*
 * nlibc - libc/src/imagem.c
 * gr_carregar: abre PNG, JPEG, BMP, GIF (o 1º quadro), TGA e PPM com a
 * stb_image (de Sean Barrett, domínio público: libc/src/stb/stb_image.h).
 */
#include <grafico.h>
#include <stdlib.h>
#include <stdio.h>

/* Sem as partes que o NovaEra não precisa (ou ainda não tem) */
#define STBI_NO_SIMD                /* sem SSE2 à mão: o mesmo código no clang e no cproc */
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_STDIO               /* o arquivo é lido inteiro antes (a nlibc ainda não tem fseek) */
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

const char *gr_erro(void) { return stbi_failure_reason(); }

gr_imagem_t *gr_carregar(const char *caminho)
{
    FILE *f = fopen(caminho, "rb");
    if (!f) return NULL;
    size_t cap = 65536, tam = 0, n_lidos;
    unsigned char *bytes = malloc(cap);
    while (bytes && (n_lidos = fread(bytes + tam, 1, cap - tam, f)) > 0) {
        tam += n_lidos;
        if (tam == cap) {
            unsigned char *maior = realloc(bytes, cap * 2);
            if (!maior) { free(bytes); bytes = NULL; break; }
            bytes = maior;
            cap *= 2;
        }
    }
    fclose(f);
    if (!bytes) return NULL;
    int l, a, n;
    unsigned char *d = stbi_load_from_memory(bytes, (int)tam, &l, &a, &n, 4);   /* sempre RGBA */
    free(bytes);
    if (!d) return NULL;
    gr_imagem_t *im = gr_nova(l, a);
    if (!im) { stbi_image_free(d); return NULL; }
    for (long i = 0; i < (long)l * a; i++)
        im->pontos[i] = (gr_cor)d[4 * i + 3] << 24 | (gr_cor)d[4 * i] << 16 |
                        (gr_cor)d[4 * i + 1] << 8 | d[4 * i + 2];
    stbi_image_free(d);
    return im;
}
