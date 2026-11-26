/*
 * usuario/bin/echo.c - escreve os argumentos na saída padrão.
 *   echo ola mundo            na tela
 *   echo ola > /tmp/a         num arquivo (o shell liga a saída ao arquivo)
 */
#include "ulib.h"

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1) escreve(" ", COR(COR_BRANCO));
        escreve(argv[i], COR(COR_BRANCO));
    }
    escreve("\n", COR(COR_BRANCO));
    return 0;
}
