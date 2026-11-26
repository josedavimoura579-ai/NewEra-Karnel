/*
 * usuario/bin/mkdir.c - cria pastas.   mkdir /tmp/projetos
 */
#include "ulib.h"

int main(int argc, char **argv)
{
    int erro = 0;
    for (int i = 1; i < argc; i++) {
        if (arq_pasta(argv[i]) < 0) {
            escreve_erro("mkdir: nao consegui criar: ");
            escreve_erro(argv[i]);
            escreve_erro("\n");
            erro = 1;
        }
    }
    if (argc < 2) escreve("uso: mkdir <pasta>...\n", COR(COR_CINZA_CLARO));
    return erro;
}
