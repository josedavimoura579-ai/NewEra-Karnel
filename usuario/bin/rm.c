/*
 * usuario/bin/rm.c - apaga arquivos (e pastas vazias).   rm /tmp/a
 */
#include "ulib.h"

int main(int argc, char **argv)
{
    int erro = 0;
    for (int i = 1; i < argc; i++) {
        if (arq_apagar(argv[i]) < 0) {
            escreve_erro("rm: nao consegui apagar (nao existe ou e pasta com arquivos): ");
            escreve_erro(argv[i]);
            escreve_erro("\n");
            erro = 1;
        }
    }
    if (argc < 2) escreve("uso: rm <arquivo>...\n", COR(COR_CINZA_CLARO));
    return erro;
}
