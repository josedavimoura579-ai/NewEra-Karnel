/*
 * usuario/bin/bloco.c - um bloco de notas, numa janela.
 *
 * Roda dentro do servidor de janelas (janelas). O que se digita aparece
 * com as letras bonitas (DejaVu Sans); Backspace apaga; Enter pula linha.
 * O teclado só chega aqui quando a janela está na frente.
 */
#include <stdio.h>
#include <string.h>
#include <janela.h>

#define LARG 440
#define ALT  260
#define TAM  17
#define MAX  2000

static jn_janela_t *j;
static gr_fonte_t *f;
static char texto[MAX + 1];
static int n, ativa = 1;

static void desenhar(void)
{
    gr_preencher(j->im, 0, 0, LARG, ALT, GR_RGB(255, 253, 240));
    for (int y = 40; y < ALT; y += gr_fonte_altura(f, TAM))         /* as linhas do caderno */
        gr_preencher(j->im, 10, y + 2, LARG - 20, 1, GR_RGB(220, 226, 240));
    /* quebra as linhas onde não cabem mais palavras */
    int x = 12, y = 20, alt = gr_fonte_altura(f, TAM);
    char pedaco[8];
    const char *p = texto;
    while (*p) {
        int k = 1;
        while ((p[k] & 0xC0) == 0x80) k++;                         /* um caractere UTF-8 inteiro */
        memcpy(pedaco, p, (size_t)k);
        pedaco[k] = 0;
        p += k;
        if (pedaco[0] == '\n') { x = 12; y += alt; continue; }
        int l = gr_fonte_largura(f, pedaco, TAM);
        if (x + l > LARG - 12) { x = 12; y += alt; }
        gr_fonte_texto(j->im, f, x, y, pedaco, TAM, GR_RGB(30, 40, 60));
        x += l;
    }
    if (ativa) gr_preencher(j->im, x + 1, y + 3, 2, alt - 6, GR_AZUL);   /* o cursor */
    if (!n) gr_fonte_texto(j->im, f, 12, 20, "Digite aqui...", TAM, GR_RGB(170, 175, 190));
    jn_mostrar_tudo(j);
}

int main(void)
{
    f = gr_fonte_abrir(GR_FONTE_NORMAL);
    j = jn_abrir("Bloco de notas", LARG, ALT);
    if (!j || !f) { fprintf(stderr, "bloco: o servidor de janelas não está rodando (rode: janelas)\n"); return 1; }
    desenhar();
    jn_evento_t e;
    while (jn_esperar(j, &e) && e.tipo != JN_FECHAR) {
        if (e.tipo == JN_FOCO) { ativa = e.x; desenhar(); }
        if (e.tipo != JN_TECLA) continue;
        if (e.tecla == '\b') {                         /* a última letra (UTF-8: 1 a 3 bytes) */
            while (n > 0 && ((unsigned char)texto[n - 1] & 0xC0) == 0x80) texto[--n] = 0;
            if (n) texto[--n] = 0;
        } else if ((e.tecla == '\n' || (e.tecla >= 32 && e.tecla != 127 && e.tecla < 0xE000)) && n + 4 < MAX) {
            n += jn_utf8(e.tecla, texto + n);
            texto[n] = 0;
        }
        desenhar();
    }
    jn_fechar(j);
    return 0;
}
