/*
 * exemplos/primos.c - os números primos até N (o crivo de Eratóstenes).
 *
 *   cproc -o /tmp/primos /usr/share/nefs/exemplos/primos.c
 *   /tmp/primos 100
 */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 50;
    if (n < 2) {
        fprintf(stderr, "uso: primos N (N >= 2)\n");
        return 1;
    }
    char *composto = calloc((size_t)n + 1, 1);
    if (!composto) return 1;
    int total = 0;
    for (int i = 2; i <= n; i++) {
        if (composto[i]) continue;
        printf("%d ", i);
        total++;
        for (long j = (long)i * i; j <= n; j += i) composto[j] = 1;
    }
    printf("\n%d primos até %d\n", total, n);
    free(composto);
    return 0;
}
