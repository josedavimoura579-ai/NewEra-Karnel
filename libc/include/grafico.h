/*
 * nlibc - grafico.h: desenhar (no modo gráfico do NovaEra).
 *
 * Uma imagem é um retângulo de pontos, cada um uma cor 0xAARRGGBB: A é a
 * opacidade (255 = opaca, 0 = invisível), R, G e B o vermelho, o verde e o
 * azul (0 a 255). Desenha-se numa imagem na memória; para aparecer, a
 * imagem da TELA (gr_tela_abrir) é mostrada com gr_tela_mostrar.
 *
 *   gr_imagem_t *tela = gr_tela_abrir();
 *   gr_preencher(tela, 0, 0, tela->largura, tela->altura, GR_RGB(11, 30, 63));
 *   gr_disco(tela, 512, 384, 100, GR_RGB(255, 160, 0));
 *   gr_texto(tela, 20, 20, "Olá!", GR_BRANCO, 3);
 *   gr_tela_mostrar_tudo(tela);
 *   ...
 *   gr_tela_fechar(tela);
 *
 * As imagens de arquivo (PNG, JPEG, BMP, GIF, TGA, PPM) vêm da stb_image
 * (domínio público). Compile com:  cproc -o prog prog.c
 */
#ifndef _GRAFICO_H
#define _GRAFICO_H

typedef unsigned int gr_cor;

#define GR_RGB(r, g, b)     ((gr_cor)(0xFF000000u | ((unsigned)(r) & 255) << 16 | ((unsigned)(g) & 255) << 8 | ((unsigned)(b) & 255)))
#define GR_RGBA(r, g, b, a) ((gr_cor)(((unsigned)(a) & 255) << 24 | ((unsigned)(r) & 255) << 16 | ((unsigned)(g) & 255) << 8 | ((unsigned)(b) & 255)))
#define GR_PRETO     GR_RGB(0, 0, 0)
#define GR_BRANCO    GR_RGB(255, 255, 255)
#define GR_VERMELHO  GR_RGB(220, 50, 50)
#define GR_VERDE     GR_RGB(46, 158, 79)
#define GR_AZUL      GR_RGB(36, 87, 197)
#define GR_AMARELO   GR_RGB(255, 201, 74)
#define GR_LARANJA   GR_RGB(255, 106, 0)
#define GR_CIANO     GR_RGB(46, 196, 214)
#define GR_NOITE     GR_RGB(11, 30, 63)           /* o azul da logo */

typedef struct {
    gr_cor *pontos;
    int     largura, altura;
    int     passo;                  /* pontos por linha na memória */
    int     _tela;                  /* (uso interno: o objeto da tela cheia) */
} gr_imagem_t;

/* Imagens na memória */
gr_imagem_t *gr_nova(int largura, int altura);          /* começa transparente */
gr_imagem_t *gr_carregar(const char *caminho);          /* PNG, JPEG, BMP...; NULL se não deu */
const char  *gr_erro(void);                             /* por que o gr_carregar falhou */
int          gr_salvar_ppm(const gr_imagem_t *im, const char *caminho);
void         gr_liberar(gr_imagem_t *im);

/* Desenhar (cores com A < 255 se misturam com o que já está lá) */
void   gr_ponto(gr_imagem_t *im, int x, int y, gr_cor c);
gr_cor gr_ler(const gr_imagem_t *im, int x, int y);
void   gr_linha(gr_imagem_t *im, int x0, int y0, int x1, int y1, gr_cor c);
void   gr_retangulo(gr_imagem_t *im, int x, int y, int l, int a, gr_cor c);     /* o contorno */
void   gr_preencher(gr_imagem_t *im, int x, int y, int l, int a, gr_cor c);
void   gr_circulo(gr_imagem_t *im, int cx, int cy, int r, gr_cor c);            /* o contorno */
void   gr_disco(gr_imagem_t *im, int cx, int cy, int r, gr_cor c);              /* cheio */
void   gr_degrade(gr_imagem_t *im, int x, int y, int l, int a, gr_cor de, gr_cor ate, int vertical);
void   gr_copiar(gr_imagem_t *dest, int x, int y, const gr_imagem_t *orig);     /* com transparência */
void   gr_escalar(gr_imagem_t *dest, int x, int y, int l, int a, const gr_imagem_t *orig);
int    gr_texto(gr_imagem_t *im, int x, int y, const char *utf8, gr_cor c, int escala);   /* devolve a largura */

/* Letras bonitas: fontes TrueType (.ttf) pela stb_truetype (domínio
 * público), com o contorno suave. O tamanho é a altura da letra em pontos.
 *   gr_fonte_t *f = gr_fonte_abrir(GR_FONTE_NORMAL);
 *   gr_fonte_texto(tela, f, 20, 20, "Ação, coração!", 32, GR_BRANCO);
 * (x, y) é o canto de cima à esquerda da linha; '\n' pula linha. */
#define GR_FONTE_NORMAL   "/usr/share/fonts/dejavu/DejaVuSans.ttf"
#define GR_FONTE_NEGRITO  "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf"
typedef struct gr_fonte gr_fonte_t;
gr_fonte_t *gr_fonte_abrir(const char *caminho);        /* NULL se não deu */
void gr_fonte_fechar(gr_fonte_t *f);
int  gr_fonte_texto(gr_imagem_t *im, gr_fonte_t *f, int x, int y, const char *utf8, int tamanho, gr_cor c);
int  gr_fonte_largura(gr_fonte_t *f, const char *utf8, int tamanho);   /* da linha mais larga */
int  gr_fonte_altura(gr_fonte_t *f, int tamanho);       /* de uma linha (com o espaço entre linhas) */

/* A tela (só no modo gráfico: bash run.sh grafico ou vnc) */
gr_imagem_t *gr_tela_abrir(void);                       /* NULL se não há tela gráfica */
void gr_tela_mostrar(gr_imagem_t *tela, int x, int y, int l, int a);
void gr_tela_mostrar_tudo(gr_imagem_t *tela);
void gr_tela_fechar(gr_imagem_t *tela);                 /* o terminal volta */

/* O mouse: espera ele fazer algo; devolve o giro da roda */
int gr_mouse(int *x, int *y, int *botoes);
#define GR_ESQUERDO 1
#define GR_DIREITO  2
#define GR_MEIO     4
#endif
