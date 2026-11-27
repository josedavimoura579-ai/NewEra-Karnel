/*
 * usuario/bin/relogio.c - um relógio de ponteiros, numa janela.
 *
 * Roda dentro do servidor de janelas (janelas). A hora vem do relógio do
 * computador, com o fuso do /etc/fuso. Puxe o canto de baixo da janela:
 * o relógio cresce junto (JN_REDIMENSIONAR).
 */
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <janela.h>

static jn_janela_t *j;

/* um ponteiro: do centro até r, no ângulo de "fração" de volta (0 = 12 horas) */
static void ponteiro(int cx, int cy, double fracao, int r, int grossura, gr_cor cor)
{
    double ang = fracao * 2 * M_PI;
    int x = cx + (int)(sin(ang) * r), y = cy - (int)(cos(ang) * r);
    for (int k = -grossura / 2; k <= grossura / 2; k++)
        for (int m = -grossura / 2; m <= grossura / 2; m++) gr_linha(j->im, cx + k, cy + m, x + k, y + m, cor);
}

static void desenhar(struct tm *t)
{
    static const char *const dias[7] = { "domingo", "segunda", "terça", "quarta", "quinta", "sexta", "sábado" };
    int L = j->largura, A = j->altura;
    gr_fonte_t *f = jn_fonte(0), *n = jn_fonte(1);
    int rodape = A / 6 > 28 ? A / 6 : 28;
    gr_degrade(j->im, 0, 0, L, A, GR_RGB(16, 34, 70), GR_RGB(8, 18, 38), 1);
    int r = (L < A - rodape ? L : A - rodape) / 2 - 10, cx = L / 2, cy = (A - rodape) / 2;
    if (r < 10) r = 10;
    gr_disco(j->im, cx + 4, cy + 5, r, GR_RGBA(0, 0, 0, 100));             /* a sombra */
    gr_disco(j->im, cx, cy, r, GR_RGB(245, 247, 250));
    gr_circulo(j->im, cx, cy, r, GR_RGB(255, 106, 0));
    gr_circulo(j->im, cx, cy, r - 1, GR_RGB(255, 106, 0));
    for (int i = 0; i < 60; i++) {                                         /* as marcas */
        double a = i * M_PI / 30;
        int de = i % 5 ? r - r / 16 : r - r / 7;
        gr_linha(j->im, cx + (int)(sin(a) * de), cy - (int)(cos(a) * de),
                 cx + (int)(sin(a) * (r - 4)), cy - (int)(cos(a) * (r - 4)), i % 5 ? GR_RGB(170, 178, 192) : GR_NOITE);
    }
    int tn = r / 6 > 10 ? r / 6 : 10;
    for (int h = 1; h <= 12; h++) {                                        /* os números */
        char s[4];
        snprintf(s, sizeof s, "%d", h);
        double a = h * M_PI / 6;
        int d = r - r / 7 - tn;
        int x = cx + (int)(sin(a) * d), y = cy - (int)(cos(a) * d);
        gr_fonte_texto(j->im, n, x - gr_fonte_largura(n, s, tn) / 2, y - gr_fonte_altura(n, tn) / 2, s, tn, GR_NOITE);
    }
    double seg = t->tm_sec, min = t->tm_min + seg / 60, hora = t->tm_hour % 12 + min / 60;
    ponteiro(cx, cy, hora / 12, r * 5 / 10, r / 18 + 2, GR_NOITE);
    ponteiro(cx, cy, min / 60, r * 8 / 10, r / 28 + 2, GR_NOITE);
    ponteiro(cx, cy, seg / 60, r * 9 / 10, 1, GR_RGB(255, 106, 0));
    gr_disco(j->im, cx, cy, r / 20 + 3, GR_RGB(255, 106, 0));

    char s[64];
    snprintf(s, sizeof s, "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
    int tt = rodape / 2;
    gr_fonte_texto(j->im, n, (L - gr_fonte_largura(n, s, tt)) / 2, A - rodape, s, tt, GR_BRANCO);
    snprintf(s, sizeof s, "%s, %02d/%02d/%d", dias[t->tm_wday], t->tm_mday, t->tm_mon + 1, t->tm_year + 1900);
    int td = rodape / 3 > 11 ? rodape / 3 : 11;
    gr_fonte_texto(j->im, f, (L - gr_fonte_largura(f, s, td)) / 2, A - rodape + tt + 4, s, td, GR_RGB(159, 179, 207));
    jn_mostrar_tudo(j);
}

int main(void)
{
    j = jn_abrir_com("Relógio", 240, 280, JN_REDIMENSIONAVEL);
    if (!j) { fprintf(stderr, "relogio: o servidor de janelas não está rodando (rode: janelas)\n"); return 1; }
    time_t antes = -1;
    for (;;) {
        int redesenhar = 0;
        while (jn_tem_evento(j)) {
            jn_evento_t e;
            if (!jn_esperar(j, &e) || e.tipo == JN_FECHAR) { jn_fechar(j); return 0; }
            if (e.tipo == JN_REDIMENSIONAR) redesenhar = 1;
        }
        time_t agora = time(NULL);
        if (agora != antes || redesenhar) { antes = agora; desenhar(localtime(&agora)); }
        usleep(200000);
    }
}
