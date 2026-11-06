/*
 * usuario/bin/falha.c - um programa com bug: lê o endereço 0.
 * O núcleo o encerra (falha de página), o shell avisa e segue em frente.
 */
#include "ulib.h"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    escreve("Vou ler o endereco 0...\n", COR(COR_AMARELO));
    return (int)mem_le32(0);
}
