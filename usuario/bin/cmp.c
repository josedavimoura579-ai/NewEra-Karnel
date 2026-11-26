/*
 * usuario/bin/cmp.c - compara dois arquivos, byte a byte.
 *   cmp a b      nada (e código 0) se forem iguais; senão, onde diferem
 *                (e código 1)
 * Em C padrão, com a libc do NovaEra.
 */
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "uso: cmp arquivo1 arquivo2\n");
        return 2;
    }
    FILE *a = fopen(argv[1], "rb"), *b = fopen(argv[2], "rb");
    if (!a || !b) {
        fprintf(stderr, "cmp: não consegui abrir %s\n", !a ? argv[1] : argv[2]);
        return 2;
    }
    long pos = 1;
    for (;; pos++) {
        int ca = fgetc(a), cb = fgetc(b);
        if (ca == EOF && cb == EOF) break;
        if (ca == EOF || cb == EOF) {
            printf("cmp: %s é menor\n", ca == EOF ? argv[1] : argv[2]);
            return 1;
        }
        if (ca != cb) {
            printf("%s e %s diferem no byte %ld\n", argv[1], argv[2], pos);
            return 1;
        }
    }
    printf("%s e %s são iguais (%ld bytes)\n", argv[1], argv[2], pos - 1);
    return 0;
}
