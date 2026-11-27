/*
 * usuario/bin/rabisco.c - desenhar com o mouse, numa janela.
 *
 * Roda dentro do servidor de janelas (janelas). Arraste com o botão
 * esquerdo para desenhar; clique numa cor da paleta para trocar; o botão
 * direito (ou a tecla c) limpa. Um exemplo de janela.h: os cliques chegam
 * com a posição DENTRO da janela.
 */
#include <stdio.h>
#include <stdlib.h>
#include <janela.h>

#define LARG  420
#define ALT   320
#define PALETA 30

static const gr_cor cores[] = { GR_PRETO, GR_VERMELHO, GR_LARANJA, GR_AMARELO, GR_VERDE, GR_AZUL, GR_CIANO };
#define N_CORES 7
static int cor;
static jn_janela_t *j;

static void paleta(void)
{
    gr_preencher(j->im, 0, 0, LARG, PALETA, GR_RGB(225, 230, 238));
    for (int i = 0; i < N_CORES; i++) {
        gr_preencher(j->im, 6 + i * 30, 5, 22, 20, cores[i]);
        if (i == cor) gr_retangulo(j->im, 4 + i * 30, 3, 26, 24, GR_NOITE);
    }
    gr_preencher(j->im, 0, PALETA - 1, LARG, 1, GR_RGB(180, 188, 200));
    jn_mostrar(j, 0, 0, LARG, PALETA);
}

static void limpar(void)
{
    gr_preencher(j->im, 0, PALETA, LARG, ALT - PALETA, GR_BRANCO);
    jn_mostrar(j, 0, PALETA, LARG, ALT - PALETA);
}

/* uma linha grossa: um disco em cada ponto do caminho */
static void traco(int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), dy = abs(y1 - y0), passos = dx > dy ? dx : dy;
    for (int k = 0; k <= passos; k++) {
        int x = passos ? x0 + (x1 - x0) * k / passos : x0, y = passos ? y0 + (y1 - y0) * k / passos : y0;
        if (y - 3 >= PALETA) gr_disco(j->im, x, y, 3, cores[cor]);
    }
    int x = (x0 < x1 ? x0 : x1) - 4, y = (y0 < y1 ? y0 : y1) - 4;
    jn_mostrar(j, x, y, dx + 9, dy + 9);
}

int main(void)
{
    j = jn_abrir("Rabisco", LARG, ALT);
    if (!j) { fprintf(stderr, "rabisco: o servidor de janelas não está rodando (rode: janelas)\n"); return 1; }
    paleta();
    limpar();
    int ux = -1, uy = -1;
    jn_evento_t e;
    while (jn_esperar(j, &e) && e.tipo != JN_FECHAR) {
        if (e.tipo == JN_TECLA && e.tecla == 'c') limpar();
        if (e.tipo != JN_MOUSE) continue;
        if (e.evento == JN_APERTOU && (e.botoes & GR_DIREITO)) { limpar(); continue; }
        if (e.evento == JN_APERTOU && e.y < PALETA) {
            int i = (e.x - 6) / 30;
            if (i >= 0 && i < N_CORES) { cor = i; paleta(); }
            continue;
        }
        if (e.botoes & GR_ESQUERDO) {
            if (e.evento == JN_APERTOU || ux < 0) traco(e.x, e.y, e.x, e.y);
            else traco(ux, uy, e.x, e.y);
            ux = e.x; uy = e.y;
        } else ux = -1;
    }
    jn_fechar(j);
    return 0;
}
