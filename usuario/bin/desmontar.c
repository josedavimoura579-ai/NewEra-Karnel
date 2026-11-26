/*
 * usuario/bin/desmontar.c - solta um disco montado (como o umount do
 * Linux): grava o que estava no cache e tira a pasta da tabela.
 *
 *   desmontar /mnt          pela pasta
 *   desmontar sdb1          ou pelo dispositivo
 */
#include <stdio.h>
#include "ulib.h"

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "uso: desmontar PASTA|DISPOSITIVO\n"); return 1; }
    char alvo[CAMINHO_MAX], pasta[CAMINHO_MAX];
    long dev_pedido = dispositivo_de(argv[1]);
    if (dev_pedido < 0 && caminho_absoluto(argv[1], alvo) < 0) {
        fprintf(stderr, "desmontar: caminho inválido\n");
        return 1;
    }
    u32 srv, dev;
    for (u32 i = 0; montagem(i, pasta, &srv, &dev) == 0; i++) {
        if (dev_pedido >= 0 ? dev != (u32)dev_pedido : !str_igual(pasta, alvo)) continue;
        long r = desmontar_de(srv, pasta);
        if (r == ERRO_OCUPADO) {
            fprintf(stderr, "desmontar: %s está em uso (há arquivos abertos)\n", pasta);
            return 1;
        }
        if (r < 0) { fprintf(stderr, "desmontar: não deu (erro %ld)\n", -r); return 1; }
        return 0;
    }
    fprintf(stderr, "desmontar: %s não está montado (veja: montar)\n", argv[1]);
    return 1;
}
