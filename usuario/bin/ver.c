/*
 * usuario/bin/ver.c - mostra uma imagem na tela inteira (só no modo gráfico).
 *
 *   ver foto.png                   PNG, JPEG, BMP, GIF, TGA ou PPM
 *   ver /usr/share/nefs/exemplos/imagens/logo.png
 *
 * A imagem aparece no meio da tela, diminuída se for maior que ela. Para
 * voltar: clique, ou aperte Enter.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <grafico.h>

static gr_imagem_t *tela;

/* Uma thread espera o Enter; a outra (a principal), o clique. */
static void *esperar_enter(void *x)
{
    (void)x;
    int c;
    while ((c = getchar()) != EOF && c != '\n') { }
    gr_tela_fechar(tela);
    exit(0);
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "uso: ver IMAGEM   (PNG, JPEG, BMP, GIF...)\n"); return 1; }
    gr_imagem_t *im = gr_carregar(argv[1]);
    if (!im) { fprintf(stderr, "ver: não consegui abrir %s (%s)\n", argv[1], gr_erro()); return 1; }
    tela = gr_tela_abrir();
    if (!tela) {
        fprintf(stderr, "ver: só no modo gráfico (bash run.sh grafico ou vnc)\n");
        return 1;
    }
    int L = tela->largura, A = tela->altura, barra = 32;
    gr_degrade(tela, 0, 0, L, A - barra, GR_NOITE, GR_RGB(27, 58, 107), 1);

    /* cabe na tela? se não, diminui mantendo a proporção */
    int l = im->largura, a = im->altura, max_l = L - 40, max_a = A - barra - 40;
    if (l > max_l) { a = (int)((long)a * max_l / l); l = max_l; }
    if (a > max_a) { l = (int)((long)l * max_a / a); a = max_a; }
    int x = (L - l) / 2, y = (A - barra - a) / 2;
    gr_preencher(tela, x + 6, y + 6, l, a, GR_RGBA(0, 0, 0, 90));          /* a sombra */
    if (l == im->largura && a == im->altura) gr_copiar(tela, x, y, im);
    else gr_escalar(tela, x, y, l, a, im);

    gr_preencher(tela, 0, A - barra, L, barra, GR_RGB(8, 18, 38));
    gr_preencher(tela, 0, A - barra, L, 2, GR_CIANO);
    char legenda[160];
    const char *nome = strrchr(argv[1], '/') ? strrchr(argv[1], '/') + 1 : argv[1];
    snprintf(legenda, sizeof legenda, "%s  -  %d x %d%s", nome, im->largura, im->altura,
             l != im->largura ? "  (diminuída para caber)" : "");
    gr_texto(tela, 12, A - barra + 9, legenda, GR_BRANCO, 1);
    const char *dica = "clique ou Enter para voltar";
    gr_texto(tela, L - 12 - 8 * (int)strlen(dica), A - barra + 9, dica, GR_RGB(159, 179, 207), 1);
    gr_tela_mostrar_tudo(tela);

    pthread_t t;
    pthread_create(&t, NULL, esperar_enter, NULL);
    int b = 0, antes = 0;
    for (;;) {                                         /* espera um clique (apertar e soltar) */
        gr_mouse(NULL, NULL, &b);
        if (antes && !b) break;
        antes = b;
    }
    gr_tela_fechar(tela);
    return 0;
}
