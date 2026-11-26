/*
 * usuario/bin/formatar.c - prepara um disco ou uma partição com o sistema
 * de arquivos FAT32 (como o mkfs.fat do Linux). APAGA TUDO o que havia lá.
 *
 *   formatar               o disco de /disco (ou o sda, se nada estiver lá)
 *   formatar sdb1          uma partição (veja: discos)
 *   formatar -sim sdb1     sem perguntar
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

int main(int argc, char **argv)
{
    int sim = 0, i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-sim")) sim = 1;
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
            if (strcmp(argv[++i], "fat32")) {
                fprintf(stderr, "formatar: só sei fazer fat32 (o ext2 vem na próxima etapa)\n");
                return 1;
            }
        } else { fprintf(stderr, "uso: formatar [-sim] [-t fat32] [DISPOSITIVO]\n"); return 1; }
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
    } else if (m.arg[2] != PART_FAT32 && m.arg[2] != 0x0B) {
        printf("Aviso: o tipo de %s na tabela é %02x, não FAT32 (0c).\n", nome, m.arg[2]);
    }
    u64 mib = (u64)total * SETOR / (1024 * 1024);
    if (total < 66600) {                     /* o FAT32 precisa de 65525 clusters */
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

    /* montado? solta antes (o servidor não pode gravar o cache por cima) */
    montado = montado_em(dev, pasta);
    if (montado && desmontar_de(PID_FAT, pasta) < 0) {
        fprintf(stderr, "formatar: %s está em uso em %s (há arquivos abertos)\n", nome, pasta);
        return 1;
    }
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
    if (r) { fprintf(stderr, "formatar: erro ao escrever no disco\n"); return 1; }

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
