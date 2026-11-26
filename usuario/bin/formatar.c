/*
 * usuario/bin/formatar.c - prepara um disco ou uma partição com um sistema
 * de arquivos: FAT32 (como o mkfs.fat do Linux) ou ext2 (como o
 * mkfs.ext2). APAGA TUDO o que havia lá.
 *
 *   formatar               o disco de /disco (ou o sda, se nada estiver lá)
 *   formatar sdb1          uma partição (veja: discos)
 *   formatar -sim sdb1     sem perguntar
 *   formatar -t ext2 sdb2  ext2 (dono e permissões; o do Linux)
 *
 * Um disco inteiro fica sem tabela de partições (como um pendrive
 * formatado no Linux com "mkfs.fat -F 32 /dev/sdb"): o setor 0 já é o do
 * FAT32. Numa partição, o setor 0 dela. O Windows, o Linux e o mtools leem
 * os dois. Se estava montado, desmonta antes e monta de novo depois.
 *
 * As contas seguem a especificação da Microsoft ("FAT: General Overview
 * of On-Disk Format"): tamanho do cluster pela tabela de tamanhos do
 * disco, e o tamanho da FAT pela fórmula dela.
 */
#include <stdio.h>
#include <string.h>
#include "ulib.h"

#define SETOR 512u

static u8 zeros[64 * SETOR];

static void poe16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void poe32(u8 *p, u32 v) { poe16(p, v); poe16(p + 2, v >> 16); }
static u32  le32_(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

static u32 dev;

static int grava(u32 lba, u32 n, const void *buf)
{
    while (n) {
        u32 k = n > 64 ? 64 : n;
        msg_t m;
        msg_zera(&m);
        m.tipo = DISCO_ESCREVER;
        m.arg[0] = lba;
        m.arg[1] = k;
        m.arg[2] = (u32)(u64)buf;
        m.arg[3] = dev;
        if (sys_chamar(PID_DISCO, &m) < 0 || m.tipo == MSG_ERRO) return -1;
        lba += k;
        n -= k;
        if (buf != zeros) buf = (const u8 *)buf + k * SETOR;
    }
    return 0;
}

/* ------------------------------- FAT32 ----------------------------------- */

static int formatar_fat(u32 total, u32 inicio, const char *nome, u64 mib)
{
    /* setores por cluster, pela tabela da Microsoft */
    u32 spc = total <= 532480u ? 1 : total <= 16777216u ? 8 : total <= 33554432u ? 16
            : total <= 67108864u ? 32 : 64;
    u32 reservados = 32, nfats = 2;
    u32 t1 = total - reservados, t2 = (256 * spc + nfats) / 2;
    u32 fat_tam = (t1 + t2 - 1) / t2;
    u32 clusters = (total - reservados - nfats * fat_tam) / spc;

    /* o setor de boot (BPB) */
    u8 b[SETOR];
    memset(b, 0, sizeof b);
    b[0] = 0xEB; b[1] = 0x58; b[2] = 0x90;                /* jmp: o código de boot (não há) */
    memcpy(b + 3, "NOVAERA ", 8);
    poe16(b + 11, SETOR);
    b[13] = (u8)spc;
    poe16(b + 14, reservados);
    b[16] = (u8)nfats;
    b[21] = 0xF8;                                          /* disco fixo */
    poe16(b + 24, 63);                                     /* geometria fictícia */
    poe16(b + 26, 255);
    poe32(b + 32, total);
    poe32(b + 36, fat_tam);
    poe32(b + 44, 2);                                      /* a raiz é o cluster 2 */
    poe16(b + 48, 1);                                      /* FSInfo no setor 1 */
    poe16(b + 50, 6);                                      /* cópia do boot no setor 6 */
    b[64] = 0x80;
    b[66] = 0x29;
    poe32(b + 67, 0x4E4F5641u ^ total);                    /* número de série */
    memcpy(b + 71, "NOVAERA    ", 11);
    memcpy(b + 82, "FAT32   ", 8);
    b[90] = 0xF4;                                          /* hlt, se alguém der boot nele */
    b[510] = 0x55; b[511] = 0xAA;

    u8 fsi[SETOR];
    memset(fsi, 0, sizeof fsi);
    poe32(fsi, 0x41615252u);
    poe32(fsi + 484, 0x61417272u);
    poe32(fsi + 488, clusters - 1);                        /* livres: todos menos a raiz */
    poe32(fsi + 492, 3);                                   /* procure a partir do 3 */
    poe32(fsi + 508, 0xAA550000u);

    poe32(b + 28, inicio);                                 /* setores antes da partição */

    printf("Formatando %s: %lu MiB, clusters de %u bytes, %u clusters...\n",
           nome, (unsigned long)mib, spc * SETOR, clusters);
    int r = grava(0, reservados, zeros);
    r |= grava(0, 1, b) | grava(1, 1, fsi) | grava(6, 1, b) | grava(7, 1, fsi);
    /* as duas FATs, zeradas; o começo: as entradas 0 e 1 e a raiz (fim de cadeia) */
    r |= grava(reservados, nfats * fat_tam, zeros);
    u8 f[SETOR];
    memset(f, 0, sizeof f);
    poe32(f, 0x0FFFFFF8u);
    poe32(f + 4, 0x0FFFFFFFu);
    poe32(f + 8, 0x0FFFFFFFu);
    for (u32 k = 0; k < nfats; k++) r |= grava(reservados + k * fat_tam, 1, f);
    /* a raiz: zerada, com o nome do disco */
    u32 raiz = reservados + nfats * fat_tam;
    r |= grava(raiz, spc, zeros);
    u8 rot[SETOR];
    memset(rot, 0, sizeof rot);
    memcpy(rot, "NOVAERA    ", 11);
    rot[11] = 0x08;
    poe16(rot + 24, 0x0021);
    r |= grava(raiz, 1, rot);
    return r ? -1 : 0;

}

/* -------------------------------- ext2 ------------------------------------
 * Como o mkfs.ext2: blocos de 1 KiB (até 512 MiB) ou 4 KiB, grupos de
 * 8 × bloco blocos, inodes de 128 bytes, um inode para cada 4 KiB (ou
 * 16 KiB) de disco. Cópias do superbloco só nos grupos 0, 1 e potências de
 * 3, 5 e 7 ("sparse_super"); o tipo do arquivo vai na pasta ("filetype").
 * A raiz é o inode 2 e já nasce com a pasta lost+found (o fsck a usa). */

static u8 blk[4096];
static u8 gdt[32768];
static u8 sb[1024];

static int tem_super(u32 g)
{
    if (g <= 1) return 1;
    for (u32 b = 3; b <= 7; b += 2) {
        u32 x = g;
        while (x % b == 0) x /= b;
        if (x == 1) return 1;
    }
    return 0;
}

static void bit(u8 *mapa, u32 i) { mapa[i / 8] |= (u8)(1u << (i % 8)); }

static void registro(u8 *r, u32 ino, u32 rl, const char *nome, u32 tipo)
{
    u32 n = (u32)strlen(nome);
    poe32(r, ino);
    poe16(r + 4, rl);
    r[6] = (u8)n;
    r[7] = (u8)tipo;
    memcpy(r + 8, nome, n);
}

static int formatar_ext2(u32 total, const char *nome, u64 mib)
{
    u32 bs = total < 1048576u ? 1024 : 4096, spb = bs / SETOR;
    u32 nblocos = total / spb, primeiro = bs == 1024 ? 1 : 0, bpg = 8 * bs;
    u32 ngr = (nblocos - primeiro + bpg - 1) / bpg;
    u32 por_bloco = bs / 128, razao = bs == 1024 ? 4096 : 16384;
    u32 ipg = (u32)(((u64)nblocos * bs / razao + ngr - 1) / ngr);
    ipg = (ipg + por_bloco - 1) / por_bloco * por_bloco;
    if (ipg < 2 * por_bloco) ipg = 2 * por_bloco;
    if (ipg > 8 * bs) ipg = 8 * bs;
    u32 itb = ipg / por_bloco, gdtb = (ngr * 32 + bs - 1) / bs;
    /* o último grupo pequeno demais para a sua própria tabela: fica de fora */
    u32 ult = nblocos - primeiro - (ngr - 1) * bpg;
    if (ult < (tem_super(ngr - 1) ? 1 + gdtb : 0) + 2 + itb + 50) {
        if (ngr == 1) { fprintf(stderr, "formatar: pequeno demais para o ext2\n"); return -1; }
        ngr--;
        nblocos = primeiro + ngr * bpg;
        gdtb = (ngr * 32 + bs - 1) / bs;
    }
    if (ngr * 32 > sizeof gdt) { fprintf(stderr, "formatar: grande demais para o ext2 do NovaEra\n"); return -1; }

    printf("Formatando %s em ext2: %lu MiB, blocos de %u bytes, %u grupos, %u inodes...\n",
           nome, (unsigned long)mib, bs, ngr, ipg * ngr);
    memset(gdt, 0, sizeof gdt);
    u32 livres_b = 0, livres_i = 0, raiz_b = 0;
    int r = 0;
    for (u32 g = 0; g < ngr && !r; g++) {
        u32 ini = primeiro + g * bpg;
        u32 tam = nblocos - ini < bpg ? nblocos - ini : bpg;
        u32 meta = tem_super(g) ? 1 + gdtb : 0;
        u32 mapa_b = ini + meta, mapa_i = mapa_b + 1, tabela = mapa_b + 2;
        u32 usados = meta + 2 + itb, inodes_usados = 0;
        if (g == 0) { raiz_b = tabela + itb; usados += 2; inodes_usados = 11; }
        u8 *d = gdt + 32 * g;
        poe32(d, mapa_b);
        poe32(d + 4, mapa_i);
        poe32(d + 8, tabela);
        poe16(d + 12, tam - usados);
        poe16(d + 14, ipg - inodes_usados);
        poe16(d + 16, g == 0 ? 2 : 0);              /* a raiz e a lost+found */
        livres_b += tam - usados;
        livres_i += ipg - inodes_usados;

        memset(blk, 0, bs);                         /* o mapa dos blocos */
        for (u32 i = 0; i < usados; i++) bit(blk, i);
        for (u32 i = tam; i < 8 * bs; i++) bit(blk, i);   /* depois do fim: "usados" */
        r |= grava(mapa_b * spb, spb, blk);
        memset(blk, 0, bs);                         /* o mapa dos inodes */
        for (u32 i = 0; i < inodes_usados; i++) bit(blk, i);
        for (u32 i = ipg; i < 8 * bs; i++) bit(blk, i);
        r |= grava(mapa_i * spb, spb, blk);
        r |= grava(tabela * spb, itb * spb, zeros); /* a tabela de inodes, zerada */
    }

    /* o superbloco */
    memset(sb, 0, sizeof sb);
    poe32(sb + 0, ipg * ngr);
    poe32(sb + 4, nblocos);
    poe32(sb + 8, nblocos / 20);                    /* 5% reservados para o root */
    poe32(sb + 12, livres_b);
    poe32(sb + 16, livres_i);
    poe32(sb + 20, primeiro);
    poe32(sb + 24, bs == 1024 ? 0 : 2);
    poe32(sb + 28, bs == 1024 ? 0 : 2);
    poe32(sb + 32, bpg);
    poe32(sb + 36, bpg);
    poe32(sb + 40, ipg);
    poe16(sb + 54, 0xFFFF);                         /* sem fsck por contagem */
    poe16(sb + 56, 0xEF53);
    poe16(sb + 58, 1);                              /* limpo */
    poe16(sb + 60, 1);                              /* em erro: continua */
    poe32(sb + 76, 1);                              /* revisão 1 (dinâmica) */
    poe32(sb + 84, 11);                             /* o 1º inode comum */
    poe16(sb + 88, 128);
    poe32(sb + 96, 0x0002);                         /* filetype */
    poe32(sb + 100, 0x0001);                        /* sparse_super */
    u32 x = sys_tempo() * 2654435761u ^ total;
    for (u32 i = 0; i < 16; i++) { x = x * 1103515245u + 12345u; sb[104 + i] = (u8)(x >> 16); }
    sb[104 + 6] = (u8)((sb[104 + 6] & 0x0F) | 0x40);  /* UUID versão 4 */
    memcpy(sb + 120, "novaera", 7);

    for (u32 g = 0; g < ngr && !r; g++) {
        if (!tem_super(g)) continue;
        u32 ini = primeiro + g * bpg;
        poe16(sb + 90, g);                          /* o número do grupo desta cópia */
        if (g == 0) r |= grava(2, 2, sb);           /* o original: no byte 1024 */
        else r |= grava(ini * spb, 2, sb);
        r |= grava((ini + 1) * spb, gdtb * spb, gdt);
    }

    /* os inodes 2 (a raiz) e 11 (lost+found), no começo da tabela do grupo 0 */
    u32 tabela0 = le32_(gdt + 8);
    memset(blk, 0, sizeof blk);
    u32 n_blocos_ino = (11 * 128 + bs - 1) / bs;
    static u8 inos[2 * 4096];
    memset(inos, 0, sizeof inos);
    u8 *raiz = inos + 1 * 128, *lf = inos + 10 * 128;
    poe16(raiz + 0, 0x4000 | 0755);
    poe32(raiz + 4, bs);
    poe16(raiz + 26, 3);                            /* ".", o nome em si e o ".." da lost+found */
    poe32(raiz + 28, spb);
    poe32(raiz + 40, raiz_b);
    poe16(lf + 0, 0x4000 | 0700);
    poe32(lf + 4, bs);
    poe16(lf + 26, 2);
    poe32(lf + 28, spb);
    poe32(lf + 40, raiz_b + 1);
    r |= grava(tabela0 * spb, n_blocos_ino * spb, inos);

    memset(blk, 0, bs);                             /* a pasta raiz */
    registro(blk, 2, 12, ".", 2);
    registro(blk + 12, 2, 12, "..", 2);
    registro(blk + 24, 11, bs - 24, "lost+found", 2);
    r |= grava(raiz_b * spb, spb, blk);
    memset(blk, 0, bs);                             /* a lost+found */
    registro(blk, 11, 12, ".", 2);
    registro(blk + 12, 2, bs - 12, "..", 2);
    r |= grava((raiz_b + 1) * spb, spb, blk);
    return r ? -1 : 0;
}

int main(int argc, char **argv)
{
    int sim = 0, ext2 = 0, i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-sim")) sim = 1;
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
            i++;
            if (!strcmp(argv[i], "ext2")) ext2 = 1;
            else if (strcmp(argv[i], "fat32")) {
                fprintf(stderr, "formatar: não conheço '%s' (fat32 ou ext2)\n", argv[i]);
                return 1;
            }
        } else { fprintf(stderr, "uso: formatar [-sim] [-t fat32|ext2] [DISPOSITIVO]\n"); return 1; }
    }

    /* qual: o pedido, o que está em /disco, ou o sda */
    char pasta[CAMINHO_MAX], nome[8];
    int montado = 0, padrao = i >= argc;
    if (!padrao) {
        long d = dispositivo_de(argv[i]);
        if (d < 0) { fprintf(stderr, "formatar: dispositivo inválido: %s (ex.: sdb1; veja: discos)\n", argv[i]); return 1; }
        dev = (u32)d;
    } else {
        u32 srv, d;
        dev = DISCO_DEV(0, 0);
        int achou = 0;
        for (u32 k = 0; montagem(k, pasta, &srv, &d) == 0; k++)
            if (str_igual(pasta, ARQ_DISCO)) { dev = d; achou = 1; }
        msg_t p;
        for (u32 k = 1; !achou && k <= 4; k++)     /* sda particionado: a 1ª partição FAT */
            if (disco_info(DISCO_DEV(0, k), &p) && (p.arg[2] == PART_FAT32 || p.arg[2] == 0x0B)) {
                dev = DISCO_DEV(0, k);
                achou = 1;
            }
    }
    dispositivo_nome(dev, nome);
    msg_t m;
    u32 total = disco_info(dev, &m);
    u32 inicio = m.arg[1];
    if (!total) {
        if (padrao) fprintf(stderr, "formatar: não há disco (no QEMU: -drive file=disco.img,format=raw)\n");
        else fprintf(stderr, "formatar: %s não existe (veja: discos)\n", nome);
        return 1;
    }
    if (DEV_PARTICAO(dev) == 0) {                /* um disco com partições: formate uma delas */
        msg_t p;
        for (u32 k = 1; k <= 4; k++) {
            if (disco_info(DISCO_DEV(DEV_DISCO(dev), k), &p)) {
                fprintf(stderr, "formatar: %s tem partições; formate uma delas (%s%u) ou apague a "
                        "tabela com: particionar %s limpar\n", nome, nome, k, nome);
                return 1;
            }
        }
    } else if (!ext2 && m.arg[2] != PART_FAT32 && m.arg[2] != 0x0B) {
        printf("Aviso: o tipo de %s na tabela é %02x, não FAT32 (0c).\n", nome, m.arg[2]);
    } else if (ext2 && m.arg[2] != PART_LINUX) {
        printf("Aviso: o tipo de %s na tabela é %02x, não Linux (83).\n", nome, m.arg[2]);
    }
    u64 mib = (u64)total * SETOR / (1024 * 1024);
    if (ext2 && total < 4096) {
        fprintf(stderr, "formatar: o ext2 precisa de pelo menos 2 MiB\n");
        return 1;
    }
    if (!ext2 && total < 66600) {                     /* o FAT32 precisa de 65525 clusters */
        fprintf(stderr, "formatar: o disco tem %lu MiB; o FAT32 precisa de pelo menos 33 MiB\n",
                (unsigned long)mib);
        return 1;
    }
    if (!sim) {
        printf("Isto APAGA TUDO em %s (%lu MiB). Digite sim para continuar: ", nome, (unsigned long)mib);
        fflush(stdout);
        char r[16];
        if (!fgets(r, sizeof r, stdin) || strncmp(r, "sim", 3) != 0) {
            printf("Nada foi feito.\n");
            return 1;
        }
    }

    /* montado? solta antes (o servidor não pode gravar o cache por cima) */
    montado = montado_em(dev, pasta);
    u32 srv_antes = 0;
    for (u32 k = 0, d2; montado && montagem(k, 0, &srv_antes, &d2) == 0; k++) if (d2 == dev) break;
    if (montado && desmontar_de(srv_antes, pasta) < 0) {
        fprintf(stderr, "formatar: %s está em uso em %s (há arquivos abertos)\n", nome, pasta);
        return 1;
    }
    if ((ext2 ? formatar_ext2(total, nome, mib) : formatar_fat(total, inicio, nome, mib)) < 0) {
        fprintf(stderr, "formatar: erro ao escrever no disco\n");
        return 1;
    }

    /* monta de novo onde estava (ou em /disco, se foi o padrão) */
    if (!montado && !padrao) {
        printf("Pronto. Para usar: montar %s /mnt\n", nome);
        return 0;
    }
    if (!montado) str_copia(pasta, ARQ_DISCO, sizeof pasta);
    if (montar_em(dev, pasta) < 0) {
        fprintf(stderr, "formatar: %s foi formatado, mas não montou em %s\n", nome, pasta);
        return 1;
    }
    printf("Pronto: %s está em %s\n", nome, pasta);
    return 0;
}
