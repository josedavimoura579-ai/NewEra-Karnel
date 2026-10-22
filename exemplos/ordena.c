/*
 * exemplos/ordena.c - lê números (da entrada ou dos argumentos), ordena com
 * qsort e mostra a média, com ponto flutuante.
 *
 *   cproc -o /tmp/ordena /usr/share/nefs/exemplos/ordena.c
 *   /tmp/ordena 5 3 9 1 7
 */
#include <stdio.h>
#include <stdlib.h>

static int compara(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
    double v[64];
    int n = 0;
    for (int i = 1; i < argc && n < 64; i++) v[n++] = strtod(argv[i], NULL);
    if (n == 0) {
        fprintf(stderr, "uso: ordena números...\n");
        return 1;
    }
    qsort(v, (size_t)n, sizeof v[0], compara);
    double soma = 0;
    for (int i = 0; i < n; i++) {
        printf("%g ", v[i]);
        soma += v[i];
    }
    printf("\nmédia: %.3f\n", soma / n);
    return 0;
}
