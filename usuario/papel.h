/*
 * usuario/papel.h - o papel de parede do NovaEra (a tela de entrada e o
 * servidor de janelas usam o mesmo).
 *
 * O padrão é desenhado aqui mesmo: o céu do amanhecer, com estrelas, e o
 * sol nascendo embaixo (como na logo). Para trocar, ponha o caminho de uma
 * imagem (PNG, JPEG...) na primeira linha do /etc/papel:
 *     /home/jose/praia.jpg
 */
#ifndef PAPEL_H
#define PAPEL_H
#include <stdio.h>
#include <string.h>
#include <grafico.h>

static void papel_padrao(gr_imagem_t *t)
{
    int L = t->largura, A = t->altura;
    gr_degrade(t, 0, 0, L, A * 2 / 3, GR_RGB(8, 20, 46), GR_RGB(40, 70, 130), 1);
    gr_degrade(t, 0, A * 2 / 3, L, A - A * 2 / 3, GR_RGB(40, 70, 130), GR_RGB(255, 140, 60), 1);
    for (unsigned i = 0; i < 70; i++) {                         /* as estrelas (sempre nos mesmos lugares) */
        unsigned h = i * 2654435761u;
        gr_ponto(t, (int)(h % (unsigned)L), (int)((h >> 11) % (unsigned)(A / 2)),
                 GR_RGBA(255, 255, 255, 80 + (h >> 24) % 150));
    }
    gr_disco(t, L / 2, A + 120, 330, GR_RGBA(255, 190, 80, 60));     /* o sol, bem grande e suave */
    gr_disco(t, L / 2, A + 160, 300, GR_RGBA(255, 170, 60, 90));
}

static void papel_de_parede(gr_imagem_t *t)
{
    FILE *f = fopen("/etc/papel", "r");
    char caminho[256] = "";
    if (f) {
        if (fgets(caminho, sizeof caminho, f)) caminho[strcspn(caminho, "\r\n")] = 0;
        fclose(f);
    }
    gr_imagem_t *im = caminho[0] && caminho[0] != '#' ? gr_carregar(caminho) : NULL;
    if (!im) { papel_padrao(t); return; }
    /* cobre a tela inteira, sem deformar (corta o que sobra) */
    int l = t->largura, a = (int)((long)im->altura * t->largura / im->largura);
    if (a < t->altura) { a = t->altura; l = (int)((long)im->largura * t->altura / im->altura); }
    gr_escalar(t, (t->largura - l) / 2, (t->altura - a) / 2, l, a, im);
    gr_liberar(im);
}
#endif
