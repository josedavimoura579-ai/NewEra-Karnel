/*
 * usuario/bin/base64.c - escreve um arquivo em base64 (só letras, números,
 * '+' e '/'), ou faz o caminho de volta. Serve para passar um arquivo
 * binário por um canal de texto, como a tela ou a porta serial.
 *   base64 arquivo        codifica (linhas de 76 letras)
 *   base64 -d arquivo     decodifica
 * Sem arquivo, lê a entrada padrão. Em C padrão, com a libc do NovaEra.
 */
#include <stdio.h>
#include <string.h>

static const char letras[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int codifica(FILE *f)
{
    unsigned char b[3];
    size_t n;
    int coluna = 0;
    while ((n = fread(b, 1, 3, f)) > 0) {
        unsigned v = (unsigned)b[0] << 16 | (n > 1 ? (unsigned)b[1] << 8 : 0) | (n > 2 ? b[2] : 0);
        char s[4] = { letras[v >> 18 & 63], letras[v >> 12 & 63],
                      n > 1 ? letras[v >> 6 & 63] : '=', n > 2 ? letras[v & 63] : '=' };
        fwrite(s, 1, 4, stdout);
        if ((coluna += 4) == 76) { putchar('\n'); coluna = 0; }
        if (n < 3) break;
    }
    if (coluna) putchar('\n');
    return 0;
}

static int decodifica(FILE *f)
{
    unsigned v = 0;
    int bits = 0, c;
    while ((c = fgetc(f)) != EOF) {
        const char *p = c ? strchr(letras, c) : NULL;
        if (!p) continue;                       /* quebras de linha e '=' */
        v = v << 6 | (unsigned)(p - letras);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            putchar((int)(v >> bits & 255));
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    int volta = argc > 1 && strcmp(argv[1], "-d") == 0;
    const char *nome = argv[volta ? 2 : 1];
    if (argc > (volta ? 3 : 2)) {
        fprintf(stderr, "uso: base64 [-d] [arquivo]\n");
        return 2;
    }
    FILE *f = nome ? fopen(nome, "rb") : stdin;
    if (!f) {
        fprintf(stderr, "base64: não consegui abrir %s\n", nome);
        return 1;
    }
    return volta ? decodifica(f) : codifica(f);
}
