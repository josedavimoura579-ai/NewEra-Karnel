/*
 * usuario/bin/discos.c - lista os discos, as partições e onde estão
 * montados (como o lsblk do Linux).
 */
#include <stdio.h>
#include "ulib.h"

/* "40 MiB", "1.5 GiB" */
static void tamanho(u32 setores, char *buf, int n)
{
    u64 kib = (u64)setores / 2;
    if (kib >= 1024 * 1024) snprintf(buf, (size_t)n, "%lu.%lu GiB", (unsigned long)(kib >> 20),
                                     (unsigned long)((kib & 0xFFFFF) * 10 >> 20));
    else if (kib >= 1024)   snprintf(buf, (size_t)n, "%lu MiB", (unsigned long)(kib >> 10));
    else                    snprintf(buf, (size_t)n, "%lu KiB", (unsigned long)kib);
}

static const char *tipo_nome(u32 t)
{
    switch (t) {
    case 0x0B: case PART_FAT32: return "FAT32";
    case 0x06: case 0x0E:       return "FAT16";
    case PART_LINUX:            return "Linux";
    case PART_SWAP:             return "swap";
    case 0x05: case 0x0F:       return "estendida";
    case 0xEE:                  return "GPT";
    }
    return "?";
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    int achou = 0;
    printf("NOME   TAMANHO     TIPO        MONTADO EM\n");
    for (u32 d = 0; d < DISCOS_MAX; d++) {
        msg_t m;
        if (!disco_info(DISCO_DEV(d, 0), &m)) continue;
        achou = 1;
        char modelo[MSG_TEXTO_MAX], nome[8], tam[24], onde[CAMINHO_MAX];
        str_copia(modelo, m.texto, sizeof modelo);
        for (u32 p = 0; p <= 4; p++) {
            u32 dev = DISCO_DEV(d, p);
            u32 setores = disco_info(dev, &m);
            if (!setores) continue;
            tamanho(setores, tam, sizeof tam);
            dispositivo_nome(dev, nome);
            char tipo[16];
            if (p) snprintf(tipo, sizeof tipo, "%02x %s", m.arg[2], tipo_nome(m.arg[2]));
            else snprintf(tipo, sizeof tipo, "disco");
            printf("%s%-*s %-11s %-11s %s", p ? "`-" : "", p ? 4 : 6, nome, tam, tipo,
                   montado_em(dev, onde) ? onde : "");
            if (!p) printf("%s(%s)", montado_em(dev, onde) ? " " : "", modelo);
            printf("\n");
        }
    }
    if (!achou) printf("(nenhum disco; no QEMU: -drive file=disco.img,format=raw)\n");
    return 0;
}
