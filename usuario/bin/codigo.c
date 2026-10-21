/*
 * usuario/bin/codigo.c - termina com o código de saída pedido.
 *   codigo 7   ->  o shell mostra "[codigo terminou com codigo 7]"
 */
#include "ulib.h"

int main(int argc, char **argv)
{
    int n = 0;
    if (argc > 1)
        for (const char *p = argv[1]; *p >= '0' && *p <= '9'; p++)
            n = n * 10 + (*p - '0');
    return n;
}
