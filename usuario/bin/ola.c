/*
 * usuario/bin/ola.c - o "Olá, mundo" do NovaEra, com argumentos.
 * Fica no initrd e é executado pelo shell:  ola um dois
 */
#include "ulib.h"

int main(int argc, char **argv)
{
    escreve("Ola do processo ", COR(COR_VERDE_CLARO));
    escreve_num((u32)sys_pid(), COR(COR_BRANCO));
    escreve("! Recebi ", COR(COR_VERDE_CLARO));
    escreve_num((u32)argc, COR(COR_BRANCO));
    escreve(" argumento(s):\n", COR(COR_VERDE_CLARO));
    for (int i = 0; i < argc; i++) {
        escreve("  argv[", COR(COR_CINZA_CLARO));
        escreve_num((u32)i, COR(COR_CINZA_CLARO));
        escreve("] = ", COR(COR_CINZA_CLARO));
        escreve(argv[i], COR(COR_AMARELO));
        escreve("\n", COR(COR_BRANCO));
    }
    return 0;
}
