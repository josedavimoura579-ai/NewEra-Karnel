/*
 * nlibc - libc/src/janela-controles.c
 * Os controles de janela.h: rótulos, botões e caixas de texto. Cada um é
 * um retângulo da janela que sabe se desenhar e o que fazer com o mouse e
 * o teclado; o programa só pergunta "clicaram em qual?" (jn_tratar).
 */
#include <janela.h>
#include <string.h>

#define TAMANHO 15
#define AZUL    GR_RGB(36, 87, 197)

gr_fonte_t *jn_fonte(int negrito)
{
    static gr_fonte_t *f[2];
    static int tentou[2];
    negrito = negrito != 0;
    if (!tentou[negrito]) {
        tentou[negrito] = 1;
        f[negrito] = gr_fonte_abrir(negrito ? GR_FONTE_NEGRITO : GR_FONTE_NORMAL);
    }
    return f[negrito];
}

static jn_controle_t *novo(jn_janela_t *j, int tipo, int x, int y, int l, int a, const char *texto)
{
    if (!j || j->n_controles == JN_MAX_CONTROLES) return NULL;
    jn_controle_t *c = &j->controles[j->n_controles++];
    memset(c, 0, sizeof *c);
    c->tipo = tipo;
    c->x = x; c->y = y; c->l = l; c->a = a;
    c->tamanho = TAMANHO;
    c->alinhar = tipo == JN_BOTAO ? JN_CENTRO : JN_ESQUERDA;
    c->visivel = 1;
    if (texto) strncpy(c->texto, texto, sizeof c->texto - 1);
    return c;
}

jn_controle_t *jn_rotulo(jn_janela_t *j, int x, int y, int l, int a, const char *t) { return novo(j, JN_ROTULO, x, y, l, a, t); }
jn_controle_t *jn_botao(jn_janela_t *j, int x, int y, int l, int a, const char *t) { return novo(j, JN_BOTAO, x, y, l, a, t); }
jn_controle_t *jn_caixa(jn_janela_t *j, int x, int y, int l, int a, const char *t) { return novo(j, JN_CAIXA, x, y, l, a, t); }

void jn_controle_mover(jn_controle_t *c, int x, int y, int l, int a)
{
    if (!c) return;
    c->x = x; c->y = y; c->l = l; c->a = a;
}

/* Escreve o texto dentro do retângulo (cortado nele), alinhado */
static void escrever(gr_imagem_t *im, jn_controle_t *c, const char *t, int margem, gr_cor cor, int *fim)
{
    gr_fonte_t *f = jn_fonte(c->negrito);
    if (!f) return;
    /* uma "imagem" que é só o retângulo: o texto não sai dele */
    int x0 = c->x + margem, l = c->l - 2 * margem;
    if (x0 < 0 || c->y < 0 || x0 + l > im->largura || c->y + c->a > im->altura || l <= 0) return;
    gr_imagem_t v = { im->pontos + (long)c->y * im->passo + x0, l, c->a, im->passo, 1 };
    int lt = gr_fonte_largura(f, t, c->tamanho), at = gr_fonte_altura(f, c->tamanho);
    int x = c->alinhar == JN_CENTRO ? (l - lt) / 2 : c->alinhar == JN_DIREITA ? l - lt : 0;
    if (c->tipo == JN_CAIXA && lt > l - 4) x = l - 4 - lt;        /* texto comprido: mostra o fim */
    gr_fonte_texto(&v, f, x, (c->a - at) / 2, t, c->tamanho, cor);
    if (fim) *fim = x0 + x + lt;
}

void jn_desenhar_controle(jn_janela_t *j, jn_controle_t *c)
{
    if (!j || !c || !c->visivel) return;
    gr_imagem_t *im = j->im;
    gr_cor fundo = c->fundo ? c->fundo : j->fundo;
    gr_cor cor = c->cor ? c->cor : GR_RGB(30, 40, 60);
    switch (c->tipo) {
    case JN_ROTULO:
        gr_preencher(im, c->x, c->y, c->l, c->a, fundo);
        escrever(im, c, c->texto, 0, cor, NULL);
        break;
    case JN_BOTAO: {
        gr_preencher(im, c->x, c->y, c->l, c->a, j->fundo);
        gr_cor de = c->fundo ? c->fundo : GR_RGB(252, 252, 254), ate = c->fundo ? c->fundo : GR_RGB(222, 227, 235);
        if (c->apertado) { gr_cor t = de; de = ate; ate = t; }
        gr_degrade(im, c->x + 1, c->y + 1, c->l - 2, c->a - 2, de, ate, 1);
        if (c->apertado) gr_preencher(im, c->x + 1, c->y + 1, c->l - 2, c->a - 2, GR_RGBA(0, 0, 0, 40));
        gr_cor borda = c->apertado ? AZUL : GR_RGB(160, 170, 188);
        gr_linha(im, c->x + 2, c->y, c->x + c->l - 3, c->y, borda);                 /* cantos arredondados */
        gr_linha(im, c->x + 2, c->y + c->a - 1, c->x + c->l - 3, c->y + c->a - 1, borda);
        gr_linha(im, c->x, c->y + 2, c->x, c->y + c->a - 3, borda);
        gr_linha(im, c->x + c->l - 1, c->y + 2, c->x + c->l - 1, c->y + c->a - 3, borda);
        gr_ponto(im, c->x + 1, c->y + 1, borda);
        gr_ponto(im, c->x + c->l - 2, c->y + 1, borda);
        gr_ponto(im, c->x + 1, c->y + c->a - 2, borda);
        gr_ponto(im, c->x + c->l - 2, c->y + c->a - 2, borda);
        escrever(im, c, c->texto, 4, cor, NULL);
        break;
    }
    case JN_CAIXA: {
        gr_preencher(im, c->x, c->y, c->l, c->a, c->fundo ? c->fundo : GR_BRANCO);
        gr_retangulo(im, c->x, c->y, c->l, c->a, c->foco ? AZUL : GR_RGB(160, 170, 188));
        if (c->foco) gr_retangulo(im, c->x + 1, c->y + 1, c->l - 2, c->a - 2, GR_RGBA(36, 87, 197, 110));
        int fim = c->x + 6;
        if (c->texto[0]) escrever(im, c, c->texto, 6, cor, &fim);
        if (c->foco) gr_preencher(im, fim + 1, c->y + 5, 2, c->a - 10, AZUL);   /* o cursor */
        break;
    }
    }
    jn_mostrar(j, c->x, c->y, c->l, c->a);
}

void jn_desenhar_controles(jn_janela_t *j)
{
    if (!j) return;
    for (int i = 0; i < j->n_controles; i++) {
        jn_controle_t *c = &j->controles[i];
        if (c->visivel) jn_desenhar_controle(j, c);
    }
}

void jn_controle_texto(jn_janela_t *j, jn_controle_t *c, const char *texto)
{
    if (!c) return;
    strncpy(c->texto, texto ? texto : "", sizeof c->texto - 1);
    c->texto[sizeof c->texto - 1] = 0;
    jn_desenhar_controle(j, c);
}

static jn_controle_t *em(jn_janela_t *j, int x, int y)
{
    for (int i = j->n_controles - 1; i >= 0; i--) {
        jn_controle_t *c = &j->controles[i];
        if (c->visivel && c->tipo != JN_ROTULO && x >= c->x && x < c->x + c->l && y >= c->y && y < c->y + c->a)
            return c;
    }
    return NULL;
}

static void focar(jn_janela_t *j, jn_controle_t *c)
{
    if (j->foco == c) return;
    jn_controle_t *antes = j->foco;
    j->foco = c;
    if (antes) { antes->foco = 0; jn_desenhar_controle(j, antes); }
    if (c) { c->foco = 1; jn_desenhar_controle(j, c); }
}

jn_controle_t *jn_tratar(jn_janela_t *j, const jn_evento_t *e)
{
    if (!j || !e) return NULL;
    if (e->tipo == JN_MOUSE && e->evento == JN_APERTOU && (e->botoes & GR_ESQUERDO)) {
        jn_controle_t *c = em(j, e->x, e->y);
        if (c && c->tipo == JN_CAIXA) { focar(j, c); return NULL; }
        if (c && c->tipo == JN_BOTAO) {
            j->capturado = c;
            c->apertado = 1;
            jn_desenhar_controle(j, c);
        }
        return NULL;
    }
    if (e->tipo == JN_MOUSE && j->capturado) {
        jn_controle_t *c = j->capturado;
        int dentro = e->x >= c->x && e->x < c->x + c->l && e->y >= c->y && e->y < c->y + c->a;
        if (e->evento == JN_SOLTOU) {
            j->capturado = NULL;
            c->apertado = 0;
            jn_desenhar_controle(j, c);
            return dentro ? c : NULL;
        }
        if (dentro != c->apertado) { c->apertado = dentro; jn_desenhar_controle(j, c); }
        return NULL;
    }
    if (e->tipo == JN_TECLA && j->foco) {
        jn_controle_t *c = j->foco;
        size_t n = strlen(c->texto);
        if (e->tecla == '\n') return c;
        if (e->tecla == '\t') {                       /* a próxima caixa */
            int i = (int)(c - j->controles);
            for (int k = 1; k <= j->n_controles; k++) {
                jn_controle_t *o = &j->controles[(i + k) % j->n_controles];
                if (o->tipo == JN_CAIXA && o->visivel) { focar(j, o); break; }
            }
            return NULL;
        }
        if (e->tecla == '\b') {                      /* apaga a última letra (UTF-8: 1 a 3 bytes) */
            while (n > 0 && ((unsigned char)c->texto[n - 1] & 0xC0) == 0x80) c->texto[--n] = 0;
            if (n) c->texto[--n] = 0;
        } else if (e->tecla >= 32 && e->tecla != 127 && e->tecla < 0xE000 && !(e->botoes & JN_ALT)) {
            char u[4];
            int k = jn_utf8(e->tecla, u);
            if (n + (size_t)k + 1 >= sizeof c->texto) return NULL;
            memcpy(c->texto + n, u, (size_t)k);
            c->texto[n + (size_t)k] = 0;
        } else return NULL;
        jn_desenhar_controle(j, c);
        return NULL;
    }
    return NULL;
}
