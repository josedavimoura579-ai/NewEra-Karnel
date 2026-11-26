/*
 * usuario/bin/montar.c - liga um disco (ou uma partição) a uma pasta
 * (como o mount do Linux).
 *
 *   montar                  mostra o que está montado
 *   montar sdb1 /mnt        o que há em sdb1 (FAT32 ou ext2) aparece em /mnt
 *
 * A pasta tem de existir (mkdir /mnt/usb). Para soltar: desmontar /mnt
 */
#include <stdio.h>
#include "ulib.h"

int main(int argc, char **argv)
{
    if (argc == 1) {
        char pasta[CAMINHO_MAX], nome[8];
        u32 srv, dev;
        for (u32 i = 0; montagem(i, pasta, &srv, &dev) == 0; i++)
            printf("%s em %s (%s)\n", dispositivo_nome(dev, nome), pasta, servidor_tipo(srv));
        return 0;
    }
    identidade_t eu_;
    if (sys_identidade(0, &eu_) == 0 && eu_.uid != UID_ROOT) {
        fprintf(stderr, "%s: precisa ser o root (use: sudo %s ...)\n", argv[0], argv[0]);
        return 1;
    }
    if (argc != 3) {
        fprintf(stderr, "uso: montar [DISPOSITIVO PASTA]      ex.: montar sdb1 /mnt\n");
        return 1;
    }
    long dev = dispositivo_de(argv[1]);
    msg_t m;
    if (dev < 0 || !disco_info((u32)dev, &m)) {
        fprintf(stderr, "montar: %s não existe (veja: discos)\n", argv[1]);
        return 1;
    }
    char pasta[CAMINHO_MAX];
    if (caminho_absoluto(argv[2], pasta) < 0) { fprintf(stderr, "montar: caminho inválido\n"); return 1; }
    long r = montar_em((u32)dev, pasta);
    if (r == ERRO_FORMATO)
        fprintf(stderr, "montar: %s não tem FAT32 nem ext2 (prepare com: formatar %s)\n", argv[1], argv[1]);
    else if (r == ERRO_NAO_ACHADO)
        fprintf(stderr, "montar: a pasta %s não existe (crie com: mkdir %s)\n", pasta, pasta);
    else if (r == ERRO_OCUPADO)
        fprintf(stderr, "montar: %s ou %s já está montado\n", argv[1], pasta);
    else if (r < 0)
        fprintf(stderr, "montar: não deu (erro %ld)\n", -r);
    return r < 0;
}
