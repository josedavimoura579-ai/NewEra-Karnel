/*
 * usuario/bin/letras.c - mostra as letras bonitas (só no modo gráfico).
 *
 *   letras                 a fonte DejaVu Sans em vários tamanhos
 *   letras FONTE.ttf       outra fonte TrueType
 *
 * As letras saem da stb_truetype, com o contorno suave. Para voltar:
 * clique, ou aperte Enter.
 */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <grafico.h>

static gr_imagem_t *tela;

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
    const char *caminho = argc > 1 ? argv[1] : GR_FONTE_NORMAL;
    gr_fonte_t *f = gr_fonte_abrir(caminho), *n = argc > 1 ? f : gr_fonte_abrir(GR_FONTE_NEGRITO);
    if (!f || !n) { fprintf(stderr, "letras: não consegui abrir a fonte %s\n", f ? GR_FONTE_NEGRITO : caminho); return 1; }
    tela = gr_tela_abrir();
    if (!tela) { fprintf(stderr, "letras: só no modo gráfico (bash run.sh grafico ou vnc)\n"); return 1; }
    int L = tela->largura, A = tela->altura, barra = 32;
    gr_cor claro = GR_RGB(159, 179, 207);
    gr_degrade(tela, 0, 0, L, A - barra, GR_NOITE, GR_RGB(27, 58, 107), 1);

    /* o título: o "Era" em laranja, como na logo */
    int x = 40, y = 24;
    x += gr_fonte_texto(tela, n, x, y, "Nova", 72, GR_BRANCO);
    gr_fonte_texto(tela, n, x, y, "Era", 72, GR_LARANJA);
    y += gr_fonte_altura(n, 72);
    gr_fonte_texto(tela, f, 44, y, "Letras com o contorno suave, em qualquer tamanho.", 22, claro);
    y += gr_fonte_altura(f, 22) + 24;

    static const int tamanhos[] = { 12, 16, 24, 32, 48, 64 };
    for (int i = 0; i < 6; i++) {
        char rotulo[16];
        snprintf(rotulo, sizeof rotulo, "%d", tamanhos[i]);
        int alt = gr_fonte_altura(f, tamanhos[i]);
        gr_fonte_texto(tela, f, 40, y + (alt - gr_fonte_altura(f, 14)) / 2, rotulo, 14, claro);
        gr_fonte_texto(tela, f, 90, y, "ação, coração, Você", tamanhos[i], GR_BRANCO);
        y += alt + 4;
    }
    y += 12;
    gr_fonte_texto(tela, f, 40, y,
                   "À noite, vovô Kowalsky vê o ímã cair no pé do pinguim queixoso\n"
                   "e vovó põe açúcar no chá de tâmaras do jabuti feliz.", 20, GR_AMARELO);
    y += 2 * gr_fonte_altura(f, 20) + 16;
    gr_texto(tela, 40, y, "A fonte de antes (8x16, pontos): ação, coração, Você", claro, 1);
    y += 40;
    gr_fonte_texto(tela, n, 36, y, "Aa Çç Ãã Éé 123", 120, GR_RGBA(255, 255, 255, 200));

    /* um cartão meio transparente, à direita, em negrito */
    gr_preencher(tela, L - 300, 150, 260, 150, GR_RGBA(255, 255, 255, 40));
    gr_retangulo(tela, L - 300, 150, 260, 150, GR_CIANO);
    gr_fonte_texto(tela, n, L - 284, 166, "Negrito", 32, GR_CIANO);
    gr_fonte_texto(tela, f, L - 284, 212, "DejaVu Sans\nstb_truetype", 20, GR_BRANCO);

    gr_preencher(tela, 0, A - barra, L, barra, GR_RGB(8, 18, 38));
    gr_preencher(tela, 0, A - barra, L, 2, GR_CIANO);
    gr_fonte_texto(tela, f, 12, A - barra + 7, caminho, 15, GR_BRANCO);
    const char *dica = "clique ou Enter para voltar";
    gr_fonte_texto(tela, f, L - 12 - gr_fonte_largura(f, dica, 15), A - barra + 7, dica, 15, claro);
    gr_tela_mostrar_tudo(tela);

    pthread_t t;
    pthread_create(&t, NULL, esperar_enter, NULL);
    int b = 0, antes = 0;
    for (;;) {
        gr_mouse(NULL, NULL, &b);
        if (antes && !b) break;
        antes = b;
    }
    gr_tela_fechar(tela);
    return 0;
}
