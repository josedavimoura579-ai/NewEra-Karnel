/*
 * usuario/bin/testegrafico.c - confere a biblioteca de desenho (grafico.h).
 *
 *   testegrafico [imagens...]
 *
 * Desenha uma cena na memória (linhas, círculos, transparência, degradê,
 * texto, uma imagem ampliada) e mostra a "impressão digital" (FNV-1a) dos
 * pontos; para cada imagem dada, o tamanho e a impressão digital dela.
 * O mesmo código compila no Linux: os números têm de ser iguais nos dois.
 * Com -ppm ARQ, salva a cena num arquivo PPM.
 */
#include <stdio.h>
#include <string.h>
#include <grafico.h>

static unsigned hash(const gr_imagem_t *im)
{
    unsigned h = 2166136261u;
    for (int j = 0; j < im->altura; j++)
        for (int i = 0; i < im->largura; i++) {
            gr_cor c = gr_ler(im, i, j);
            for (int k = 0; k < 32; k += 8) { h ^= (c >> k) & 255; h *= 16777619u; }
        }
    return h;
}

int main(int argc, char **argv)
{
    const char *ppm = NULL;
    int i = 1;
    if (argc > 2 && !strcmp(argv[1], "-ppm")) { ppm = argv[2]; i = 3; }

    gr_imagem_t *cena = gr_nova(320, 240);
    gr_degrade(cena, 0, 0, 320, 240, GR_NOITE, GR_RGB(27, 58, 107), 1);
    gr_disco(cena, 160, 150, 60, GR_LARANJA);
    gr_circulo(cena, 160, 150, 70, GR_AMARELO);
    for (int k = 0; k < 12; k++) gr_linha(cena, 160, 150, 20 + k * 25, 10, GR_CIANO);
    gr_preencher(cena, 40, 160, 240, 50, GR_RGBA(255, 255, 255, 96));   /* transparente */
    gr_retangulo(cena, 40, 160, 240, 50, GR_BRANCO);
    gr_texto(cena, 52, 177, "Ação, coração!", GR_PRETO, 1);
    gr_texto(cena, 8, 8, "NovaEra", GR_BRANCO, 2);
    gr_imagem_t *pequena = gr_nova(4, 4);
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) gr_ponto(pequena, x, y, (x + y) % 2 ? GR_VERMELHO : GR_RGBA(0, 0, 255, 128));
    gr_escalar(cena, 250, 20, 60, 60, pequena);
    gr_copiar(cena, 240, 100, pequena);
    printf("cena 320x240: %08x\n", hash(cena));
    if (ppm && gr_salvar_ppm(cena, ppm) == 0) printf("salva em %s\n", ppm);

    for (; i < argc; i++) {
        gr_imagem_t *im = gr_carregar(argv[i]);
        const char *nome = strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i];
        if (!im) { printf("%s: não abriu (%s)\n", nome, gr_erro()); continue; }
        printf("%s %dx%d: %08x\n", nome, im->largura, im->altura, hash(im));
        gr_liberar(im);
    }
    return 0;
}
