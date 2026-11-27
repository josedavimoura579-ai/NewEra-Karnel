/*
 * usuario/bin/sleep.c - espera alguns segundos.
 *   sleep 5            cinco segundos (o Ctrl+C interrompe)
 *   sleep 0.5          meio segundo
 *   sleep 100 | cat    um pipe "travado": bom para testar o Ctrl+C
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "uso: sleep SEGUNDOS\n"); return 1; }
    double s = strtod(argv[1], NULL);
    if (s < 0) s = 0;
    unsigned inteiros = (unsigned)s;
    if (inteiros) sleep(inteiros);
    usleep((unsigned)((s - inteiros) * 1000000.0));
    return 0;
}
