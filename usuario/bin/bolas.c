/*
 * usuario/bin/bolas.c - bolas quicando, numa janela.
 *
 * Roda dentro do servidor de janelas (janelas). A janela se redesenha
 * sozinha, 20 vezes por segundo, enquanto as outras continuam funcionando.
 * Clique para pôr mais uma bola.
 */
#include <stdio.h>
#include <unistd.h>
#include <janela.h>

#define LARG 320
#define ALT  240
#define MAX  12

typedef struct { int x, y, vx, vy, r; gr_cor cor; } bola_t;
static bola_t bolas[MAX] = {
    { 60, 50, 3, 2, 18, GR_RGB(255, 106, 0) },
    { 200, 120, -2, 3, 26, GR_RGB(46, 196, 214) },
    { 120, 180, 4, -3, 12, GR_RGB(255, 201, 74) },
};
static int n = 3;
static const gr_cor cores[] = { GR_RGB(220, 50, 50), GR_RGB(46, 158, 79), GR_RGB(160, 90, 220), GR_BRANCO };

int main(void)
{
    jn_janela_t *j = jn_abrir("Bolas", LARG, ALT);
    if (!j) { fprintf(stderr, "bolas: o servidor de janelas não está rodando (rode: janelas)\n"); return 1; }
    gr_fonte_t *f = gr_fonte_abrir(GR_FONTE_NORMAL);
    for (;;) {
        jn_evento_t e;
        while (jn_tem_evento(j)) {
            if (!jn_esperar(j, &e) || e.tipo == JN_FECHAR) { jn_fechar(j); return 0; }
            if (e.tipo == JN_MOUSE && e.evento == JN_APERTOU && n < MAX) {
                bola_t b = { e.x, e.y, 2 + n % 3, -3 + n % 2, 10 + 3 * (n % 4), cores[n % 4] };
                bolas[n++] = b;
            }
        }
        gr_degrade(j->im, 0, 0, LARG, ALT, GR_RGB(16, 34, 70), GR_RGB(8, 18, 38), 1);
        for (int i = 0; i < n; i++) {
            bola_t *b = &bolas[i];
            b->x += b->vx;
            b->y += b->vy;
            if (b->x < b->r) { b->x = b->r; b->vx = -b->vx; }
            if (b->x > LARG - b->r) { b->x = LARG - b->r; b->vx = -b->vx; }
            if (b->y < b->r) { b->y = b->r; b->vy = -b->vy; }
            if (b->y > ALT - b->r) { b->y = ALT - b->r; b->vy = -b->vy; }
            gr_disco(j->im, b->x + 3, b->y + 4, b->r, GR_RGBA(0, 0, 0, 90));   /* a sombra */
            gr_disco(j->im, b->x, b->y, b->r, b->cor);
            gr_disco(j->im, b->x - b->r / 3, b->y - b->r / 3, b->r / 4, GR_RGBA(255, 255, 255, 120));
        }
        if (f) gr_fonte_texto(j->im, f, 8, ALT - 20, "clique para pôr mais uma", 13, GR_RGBA(255, 255, 255, 140));
        jn_mostrar_tudo(j);
        usleep(50000);
    }
}
