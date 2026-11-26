/*
 * usuario/bin/cp.c - copia um arquivo.
 *   cp origem destino        destino pode ser um arquivo ou uma pasta
 *   cp a b c /disco/pasta    vários arquivos para uma pasta
 * Em C padrão, com a libc do NovaEra.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int copia(const char *de, const char *para)
{
    FILE *a = fopen(de, "rb");
    if (!a) { fprintf(stderr, "cp: não consegui abrir %s\n", de); return 1; }
    FILE *b = fopen(para, "wb");
    if (!b) { fprintf(stderr, "cp: não consegui criar %s\n", para); fclose(a); return 1; }
    static char buf[32768];
    size_t n;
    int erro = 0;
    while ((n = fread(buf, 1, sizeof buf, a)) > 0)
        if (fwrite(buf, 1, n, b) != n) { erro = 1; break; }
    fclose(a);
    if (fclose(b) != 0) erro = 1;
    if (erro) fprintf(stderr, "cp: erro ao gravar %s (disco cheio?)\n", para);
    return erro;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "uso: cp origem destino   |   cp arquivos... pasta\n");
        return 2;
    }
    const char *destino = argv[argc - 1];
    struct stat st;
    int pasta = stat(destino, &st) == 0 && S_ISDIR(st.st_mode);
    if (argc > 3 && !pasta) { fprintf(stderr, "cp: %s não é uma pasta\n", destino); return 1; }
    int erro = 0;
    for (int i = 1; i < argc - 1; i++) {
        if (!pasta) { erro |= copia(argv[i], destino); continue; }
        const char *nome = strrchr(argv[i], '/');
        nome = nome ? nome + 1 : argv[i];
        char *c = malloc(strlen(destino) + strlen(nome) + 2);
        if (!c) return 1;
        sprintf(c, "%s/%s", destino, nome);
        erro |= copia(argv[i], c);
        free(c);
    }
    return erro;
}
