/*
 * exemplos/ola.c - o primeiro programa em C compilado DENTRO do NovaEra.
 *
 *   cproc -o /tmp/ola /usr/share/nefs/exemplos/ola.c
 *   /tmp/ola Maria
 */
#include <stdio.h>

int main(int argc, char **argv)
{
    const char *nome = argc > 1 ? argv[1] : "mundo";
    printf("Olá, %s! Este programa foi compilado dentro do NovaEra.\n", nome);
    return 0;
}
